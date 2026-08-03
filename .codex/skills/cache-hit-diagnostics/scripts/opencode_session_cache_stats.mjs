#!/usr/bin/env node
/**
 * opencode session token / prompt-cache analyzer (SQLite-backed).
 *
 * Reads opencode's session store (default ~/.local/share/opencode/opencode.db)
 * and reports per-request prompt-cache hit rates for one session, including
 * subagent sessions (parent_id children) when --subagents is given.
 *
 * Usage:
 *   node scripts/opencode_session_cache_stats.mjs --session <session-id>
 *       [--db PATH] [--head N] [--tail N] [--subagents] [--json]
 *   node scripts/opencode_session_cache_stats.mjs --workspace <dir-substring>
 *       [same flags]
 *
 * Data model: every assistant `message` row's data JSON carries
 *   tokens: { total, input, output, reasoning, cache: { read, write } }
 * where `input` is the NON-cached (new/miss) prompt portion, so
 * prompt = input + cache.read + cache.write and the cache hit rate is
 * cache.read / prompt. Rows with output<=1 and cache.read=0 are streaming
 * stub-accounting rows (unmeasurable, not misses).
 */

import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { existsSync } from "node:fs";

let DatabaseSync = null;
try {
  ({ DatabaseSync } = await import("node:sqlite"));
} catch (err) {
  console.error(`node:sqlite unavailable (${err.code ?? err.message}); need Node >= 22.5 (24+ recommended)`);
  process.exit(2);
}

function parseArgs(argv) {
  const args = {
    session: null, workspace: null, db: null,
    head: 60, tail: 60, subagents: false, json: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--session") args.session = next();
    else if (a === "--workspace") args.workspace = next();
    else if (a === "--db") args.db = next();
    else if (a === "--head") args.head = Number.parseInt(next(), 10);
    else if (a === "--tail") args.tail = Number.parseInt(next(), 10);
    else if (a === "--subagents") args.subagents = true;
    else if (a === "--json") args.json = true;
    else if (a === "--help" || a === "-h") {
      console.log(`Usage: node scripts/opencode_session_cache_stats.mjs --session ID [--db PATH] [--head N] [--tail N] [--subagents] [--json]
       node scripts/opencode_session_cache_stats.mjs --workspace TEXT [same flags]`);
      process.exit(0);
    } else throw new Error(`unknown argument "${a}"`);
  }
  if (!args.session && !args.workspace) throw new Error("need --session <id> or --workspace <text>");
  return args;
}

function defaultDbPath() {
  const data = process.env.OPENCODE_DATA
    || join(process.env.XDG_DATA_HOME || join(homedir(), ".local", "share"), "opencode");
  return join(data, "opencode.db");
}

function openDb(path) {
  if (!existsSync(path)) {
    console.error(`opencode db not found: ${path}`);
    process.exit(2);
  }
  return new DatabaseSync(path, { readOnly: true, timeout: 10000 });
}

function queryAll(db, sql, params = []) {
  const stmt = db.prepare(sql);
  return stmt.all(...params);
}

function sessionRow(db, id) {
  const rows = queryAll(db,
    `SELECT id, parent_id, agent, model, directory, time_created, time_updated,
            tokens_input, tokens_output, tokens_cache_read, tokens_cache_write
       FROM session WHERE id = ?`, [id]);
  return rows[0] ?? null;
}

function discoverSessions(db, needle) {
  return queryAll(db,
    `SELECT id, parent_id, agent, model, directory, time_created
       FROM session WHERE directory LIKE ? ORDER BY time_created`,
    [`%${needle}%`]);
}

function childSessions(db, parentId) {
  return queryAll(db,
    `SELECT id, parent_id, agent, model, directory, time_created
       FROM session WHERE parent_id = ? ORDER BY time_created`, [parentId]);
}

/** Collect the session itself plus all descendants via parent_id. */
function collectTree(db, rootId) {
  const seen = new Set([rootId]);
  const out = [];
  let frontier = [rootId];
  while (frontier.length) {
    const next = [];
    for (const id of frontier) {
      for (const c of childSessions(db, id)) {
        if (seen.has(c.id)) continue;
        seen.add(c.id);
        out.push(c);
        next.push(c.id);
      }
    }
    frontier = next;
  }
  return out;
}

function extractRequests(db, sessionIds) {
  const requests = [];
  const noUsage = new Map(sessionIds.map(id => [id, 0]));
  const placeholders = [];
  for (const id of sessionIds) {
    const rows = queryAll(db,
      `SELECT time_created, data FROM message WHERE session_id = ? ORDER BY time_created, id`, [id]);
    for (const row of rows) {
      let m = null;
      try { m = JSON.parse(row.data); } catch { continue; }
      if (m?.role !== "assistant") continue;
      const t = m.tokens;
      const input = t?.input || 0;
      const output = t?.output || 0;
      const reasoning = t?.reasoning || 0;
      const cached = t?.cache?.read || 0;
      const write = t?.cache?.write || 0;
      if (!t || (input + output + reasoning + cached + write) === 0) {
        noUsage.set(id, noUsage.get(id) + 1);
        placeholders.push({ ts: row.time_created, sessionId: id, note: "assistant message without usage tokens" });
        continue;
      }
      requests.push({
        sessionId: id,
        ts: row.time_created,
        input, output, reasoning, cached, write,
        modelID: m.modelID ?? null,
        providerID: m.providerID ?? null,
        agent: m.agent ?? m.mode ?? null,
        finish: m.finish ?? null,
        cost: m.cost ?? 0,
      });
    }
  }
  requests.sort((a, b) => a.ts - b.ts);
  return { requests, noUsage, placeholders };
}

function stats(subset) {
  let input = 0, cached = 0, write = 0, output = 0, cost = 0;
  let full = 0, zeroOnReal = 0, stub = 0, errors = 0;
  for (const r of subset) {
    if (r.input + r.cached + r.write === 0) { errors += 1; continue; }
    if (r.cached === 0 && r.output <= 1) { stub += 1; continue; } // unmeasurable stub row
    input += r.input; cached += r.cached; write += r.write;
    output += r.output; cost += r.cost;
    const prompt = r.input + r.cached + r.write;
    if (prompt && r.cached >= prompt * 0.95) full += 1;
    if (r.cached === 0 && r.output > 1) zeroOnReal += 1;
  }
  const prompt = input + cached + write;
  return {
    n: subset.length, errors, stub, input, cached, write, output, cost,
    hitRate: prompt ? (100 * cached / prompt) : 0,
    full, zeroOnReal,
  };
}

function fmtTs(ts) {
  const d = new Date(ts);
  const p = n => String(n).padStart(2, "0");
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function fmtHour(ts) {
  const d = new Date(ts);
  const p = n => String(n).padStart(2, "0");
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:00`;
}

function fmt(n) { return Number(n).toLocaleString("en-US"); }

function report(requests, args, meta) {
  const out = {
    sessionId: meta.sessionId,
    directory: meta.directory,
    agent: meta.agent,
    model: meta.model,
    rows: requests.length,
    first: requests[0]?.ts ?? null,
    last: requests.at(-1)?.ts ?? null,
    head: {}, middle: {}, tail: {}, all: {},
    anomalies: [], hourly: {}, perModel: {}, subagents: [],
  };
  const n = requests.length;
  const slice = (a, b) => requests.slice(a, b);
  out.head = stats(slice(0, args.head));
  out.tail = stats(slice(Math.max(0, n - args.tail), n));
  out.all = stats(requests);
  if (n > 0) {
    out.middle = stats(slice(Math.max(0, Math.floor(n / 2) - Math.floor(args.tail / 2)), Math.floor(n / 2) + Math.ceil(args.tail / 2)));
  }
  for (const r of requests) {
    if (r.input + r.cached + r.write === 0) {
      out.anomalies.push({ ts: r.ts, sessionId: r.sessionId, note: "no usage tokens" });
    } else if (r.cached === 0 && r.output > 1) {
      out.anomalies.push({ ts: r.ts, sessionId: r.sessionId, input: r.input, cached: r.cached, output: r.output, note: "real request with zero cache" });
    }
    const hour = fmtHour(r.ts);
    const b = out.hourly[hour] ?? (out.hourly[hour] = { n: 0, input: 0, cached: 0, stub: 0, output: 0 });
    b.n += 1; b.input += r.input; b.cached += r.cached; b.output += r.output;
    if (r.cached === 0 && r.output <= 1) b.stub += 1;

    const key = `${r.providerID ?? "?"}/${r.modelID ?? "?"}`;
    const pm = out.perModel[key] ?? (out.perModel[key] = { n: 0, input: 0, cached: 0, output: 0, stub: 0, cost: 0 });
    pm.n += 1; pm.input += r.input; pm.cached += r.cached; pm.output += r.output; pm.cost += r.cost;
    if (r.cached === 0 && r.output <= 1) pm.stub += 1;
  }
  return out;
}

function printWindow(w, label) {
  if (!w.n) { console.log(`${label}: no rows`); return; }
  const prompt = w.input + w.cached + w.write;
  console.log(`${label}: n=${w.n} (${w.errors} no-usage, ${w.stub} stub) in_new=${fmt(w.input)} cached=${fmt(w.cached)} (${w.hitRate.toFixed(1)}% of prompt ${fmt(prompt)}) out=${fmt(w.output)} | full-hit>=95%=${w.full} zero-cache-on-real=${w.zeroOnReal}`);
}

function printHuman(requests, out, args) {
  console.log(`session ${out.sessionId}: ${out.rows} requests | ${out.directory}`);
  console.log(`  agent=${out.agent ?? "?"} model=${out.model ?? "?"} | ${fmtTs(out.first)} -> ${fmtTs(out.last)}`);
  printWindow(out.head, `HEAD (first ${args.head})`);
  printWindow(out.middle, "MIDDLE");
  printWindow(out.tail, `TAIL (last ${args.tail})`);
  printWindow(out.all, "ALL");
  if (requests.length > 0) {
    console.log("\nWarm-up (first 10):");
    requests.slice(0, 10).forEach((r, i) => {
      const prompt = r.input + r.cached + r.write;
      console.log(`  #${i + 1} ${fmtTs(r.ts)} in_new=${fmt(r.input)} cached=${fmt(r.cached)} ${prompt ? (100 * r.cached / prompt).toFixed(1) : "-"}% out=${r.output} finish=${r.finish ?? "-"}`);
    });
  }
  if (out.anomalies.length) {
    console.log(`\nAnomalies (${out.anomalies.length}):`);
    for (const a of out.anomalies.slice(0, 20)) {
      console.log(`  ${fmtTs(a.ts)} ${a.sessionId} in=${a.input ?? "-"} cached=${a.cached ?? "-"} out=${a.output ?? "-"} [${a.note}]`);
    }
  }
  console.log("\nHourly (hour: n / stub / hit%):");
  for (const [hour, b] of Object.entries(out.hourly).sort()) {
    const prompt = b.input + b.cached;
    console.log(`  ${hour} | n=${b.n} | stub=${b.stub} | ${prompt ? (100 * b.cached / prompt).toFixed(1) : "-"}%`);
  }
  console.log("\nPer model (provider/model: n / in_new / cached / hit% / cost):");
  for (const [key, pm] of Object.entries(out.perModel).sort()) {
    const prompt = pm.input + pm.cached;
    console.log(`  ${key}: n=${pm.n} in_new=${fmt(pm.input)} cached=${fmt(pm.cached)} ${prompt ? (100 * pm.cached / prompt).toFixed(1) : "-"}% cost=$${pm.cost.toFixed(4)}`);
  }
}

function printSubagentsHuman(children, treeRows) {
  console.log(`\nSubagent sessions (${children.length} descendants):`);
  for (const c of children) {
    const r = treeRows.find(x => x.sessionId === c.id);
    if (!r || !r.n) { console.log(`  ${c.id} agent=${c.agent ?? "?"} ${c.model ?? "?"} | no requests`); continue; }
    const prompt = r.input + r.cached + r.write;
    console.log(`  ${c.id} agent=${c.agent ?? "?"} ${c.model ?? "?"} | n=${r.n} in_new=${fmt(r.input)} cached=${fmt(r.cached)} ${prompt ? (100 * r.cached / prompt).toFixed(1) : "-"}% out=${fmt(r.output)} stub=${r.stub}`);
  }
}

function printCombinedHuman(combined) {
  const w = combined.all;
  const prompt = w.input + w.cached + w.write;
  console.log(`\nCOMBINED (main + ${combined.subagentCount} subagent sessions): n=${combined.rows}`);
  printWindow(w, "ALL");
  printWindow(combined.tail, `TAIL (last ${combined.tailN})`);
}

try {
  const args = parseArgs(process.argv.slice(2));
  const dbPath = resolve(args.db || defaultDbPath());
  const db = openDb(dbPath);

  let sessionId = args.session;
  if (!sessionId) {
    const hits = discoverSessions(db, args.workspace);
    if (!hits.length) {
      console.error(`no opencode session with directory containing "${args.workspace}"`);
      process.exit(2);
    }
    const withRows = hits.filter(h => queryAll(db,
      `SELECT COUNT(*) AS n FROM message WHERE session_id = ?`, [h.id])[0].n > 0);
    if (!withRows.length) {
      console.error(`no sessions with messages for "${args.workspace}"; candidates:`);
      for (const h of hits) console.error(`  ${h.id} ${h.agent ?? "?"} ${h.directory}`);
      process.exit(2);
    }
    console.error(`opencode sessions with dir ~ "${args.workspace}":`);
    for (const h of withRows) console.error(`  ${h.id} ${h.agent ?? "?"} ${h.directory}`);
    const roots = withRows.filter(h => !h.parent_id);
    sessionId = (roots.length ? roots.at(-1) : withRows.at(-1)).id;
  }

  const root = sessionRow(db, sessionId);
  if (!root) {
    console.error(`session ${sessionId} not found in ${dbPath}`);
    process.exit(2);
  }

  const meta = {
    sessionId: root.id,
    directory: root.directory,
    agent: root.agent,
    model: root.model,
    timeCreated: root.time_created,
  };
  const tree = args.subagents ? collectTree(db, sessionId) : [];
  const { requests, noUsage, placeholders } = extractRequests(db, [sessionId]);
  const out = report(requests, args, meta);

  const combined = { subagentCount: tree.length, tailN: args.tail, rows: 0, all: {}, tail: {} };
  let combinedRequests = requests;
  if (args.subagents && tree.length) {
    const childExtract = extractRequests(db, tree.map(c => c.id));
    combinedRequests = [...requests, ...childExtract.requests].sort((a, b) => a.ts - b.ts);
    const cAll = report(combinedRequests, args, meta);
    combined.rows = combinedRequests.length;
    combined.all = cAll.all;
    combined.tail = cAll.tail;
  } else {
    combined.rows = requests.length;
    combined.all = out.all;
    combined.tail = out.tail;
  }

  const requestsBySession = new Map();
  for (const r of combinedRequests) {
    const arr = requestsBySession.get(r.sessionId) ?? [];
    arr.push(r);
    requestsBySession.set(r.sessionId, arr);
  }
  const childSummaries = tree.map(c => ({
    sessionId: c.id, agent: c.agent, model: c.model,
    ...stats(requestsBySession.get(c.id) ?? []),
  }));
  out.subagents = childSummaries;
  out.combined = combined;
  out.noUsage = Object.fromEntries(noUsage);
  out.placeholders = placeholders;

  if (args.json) {
    console.log(JSON.stringify(out, null, 2));
  } else {
    printHuman(requests, out, args);
    if (args.subagents) {
      printSubagentsHuman(tree, childSummaries);
      printCombinedHuman(combined);
    }
    if (Object.values(noUsage).some(n => n > 0)) {
      console.log(`\nNo-usage assistant messages: ${JSON.stringify(noUsage)}`);
    }
  }
  db.close();
  process.exit(0);
} catch (err) {
  console.error(err.message ?? String(err));
  process.exit(2);
}

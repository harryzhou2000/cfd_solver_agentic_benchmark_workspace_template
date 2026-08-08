#!/usr/bin/env node
/**
 * Session-wise token / prompt-cache analysis for a codex session.
 *
 * Correlates one codex session with the requests ocx actually sent upstream and
 * prints head/tail/middle cache statistics, the warm-up curve, anomalies, and
 * hourly buckets. Correlation mechanism: ocx persists
 * `conversationId = sha256(session_id)[:32]` (request-log-conversation.ts), so
 * this script computes the digest and filters the ocx usage log.
 *
 * Usage:
 *   node scripts/session_cache_stats.mjs --session <session-id>
 *       [--usage ~/.opencodex/usage.jsonl] [--head 60] [--tail 60]
 *   node scripts/session_cache_stats.mjs --cwd <substring>   # discover session
 *       # from ~/.codex/sessions/<yyyy>/<mm>/<dd>/rollout-*.jsonl session_meta
 *
 * Options:
 *   --session ID   Codex session id (rollout session_meta.session_id).
 *   --cwd TEXT     Find sessions whose session_meta cwd contains TEXT (prints
 *                  matches and analyzes the first with usage rows).
 *   --usage PATH   ocx usage log (default ~/.opencodex/usage.jsonl).
 *   --head N       Requests in the head window (default 60).
 *   --tail N       Requests in the tail window (default 60).
 *   --json         Machine-readable summary object.
 *   -h, --help     Show this help.
 *
 * Exit code 0 on success; 2 when the session/usage data cannot be resolved.
 */
import { createHash } from "node:crypto";
import { readFileSync, readdirSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";

function parseArgs(argv) {
  const args = { session: null, cwd: null, usage: null, head: 60, tail: 60, json: false };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--session") args.session = next();
    else if (a === "--cwd") args.cwd = next();
    else if (a === "--usage") args.usage = next();
    else if (a === "--head") args.head = Number.parseInt(next(), 10);
    else if (a === "--tail") args.tail = Number.parseInt(next(), 10);
    else if (a === "--json") args.json = true;
    else if (a === "--help" || a === "-h") {
      console.log(`Usage: node scripts/session_cache_stats.mjs --session ID [--usage PATH] [--head N] [--tail N] [--json]
       node scripts/session_cache_stats.mjs --cwd TEXT [--usage PATH] [--head N] [--tail N] [--json]`);
      process.exit(0);
    } else throw new Error(`unknown argument "${a}"`);
  }
  if (!args.session && !args.cwd) throw new Error("need --session <id> or --cwd <text>");
  return args;
}

function conversationIdFor(sessionId) {
  return createHash("sha256").update(sessionId).digest("hex").slice(0, 32);
}

function defaultUsagePath() {
  const home = process.env.OPENCODEX_HOME || join(homedir(), ".opencodex");
  return join(home, "usage.jsonl");
}

function readUsageRows(path) {
  const rows = [];
  for (const line of readFileSync(path, "utf8").split("\n")) {
    if (!line.trim()) continue;
    try { rows.push(JSON.parse(line)); } catch { /* skip malformed */ }
  }
  return rows;
}

/** Discover codex sessions whose session_meta cwd contains `needle`. Reads only
 *  the first JSONL record (session_meta is the first record in each rollout). */
function findSessionsByCwd(needle) {
  const sessionsDir = join(homedir(), ".codex", "sessions");
  const hits = [];
  for (const year of readdirSync(sessionsDir)) {
    const yearDir = join(sessionsDir, year);
    let months = [];
    try { months = readdirSync(yearDir); } catch { continue; }
    for (const month of months) {
      const monthDir = join(yearDir, month);
      let days = [];
      try { days = readdirSync(monthDir); } catch { continue; }
      for (const day of days) {
        const dayDir = join(monthDir, day);
        let files = [];
        try { files = readdirSync(dayDir).filter(f => f.endsWith(".jsonl")); } catch { continue; }
        for (const file of files) {
          try {
            const first = readFileSync(join(dayDir, file), "utf8").split("\n", 1)[0];
            const meta = JSON.parse(first);
            if (meta && String(meta.payload?.cwd ?? "").includes(needle)) {
              hits.push({ sessionId: meta.payload.session_id, cwd: meta.payload.cwd, start: meta.payload.timestamp, file: join(dayDir, file) });
            }
          } catch { /* skip */ }
        }
      }
    }
  }
  return hits.sort((a, b) => String(a.start).localeCompare(String(b.start)));
}

function stats(subset) {
  let input = 0, cached = 0, output = 0, full = 0, zeroOnReal = 0, errors = 0;
  for (const r of subset) {
    const u = r.usage ?? {};
    const tin = u.inputTokens || 0, tc = u.cachedInputTokens || 0, tout = u.outputTokens || 0;
    if (r.status !== 200 || tin === 0) { errors += 1; continue; }
    input += tin; cached += tc; output += tout;
    if (tc >= tin * 0.95) full += 1;
    if (tc === 0) zeroOnReal += 1;
  }
  return {
    n: subset.length, errors, input, cached, output,
    hitRate: input ? (100 * cached / input) : 0,
    full, zeroOnReal,
  };
}

function fmtTs(ts) {
  const d = new Date(ts);
  const p = n => String(n).padStart(2, "0");
  return `${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

function fmt(n) { return Number(n).toLocaleString("en-US"); }

function report(rows, args) {
  const out = { sessionId: args.session, rows: rows.length, first: rows[0]?.timestamp, last: rows.at(-1)?.timestamp, head: {}, tail: {}, all: {}, anomalies: [], hourly: {} };
  const n = rows.length;
  const slice = (a, b) => rows.slice(a, b);
  out.head = stats(slice(0, args.head));
  out.tail = stats(slice(Math.max(0, n - args.tail), n));
  out.all = stats(rows);
  if (n > 0) {
    const mid = slice(Math.max(0, Math.floor(n / 2) - Math.floor(args.tail / 2)), Math.floor(n / 2) + Math.ceil(args.tail / 2));
    out.middle = stats(mid);
  }
  for (const r of rows) {
    const u = r.usage ?? {};
    const tin = u.inputTokens || 0, tc = u.cachedInputTokens || 0, tout = u.outputTokens || 0;
    if (r.status !== 200) {
      out.anomalies.push({ ts: r.timestamp, requestId: r.requestId, status: r.status, in: tin, cached: tc, out: tout, durationMs: r.durationMs });
    } else if (tin > 0 && tc === 0 && tout > 1) {
      out.anomalies.push({ ts: r.timestamp, requestId: r.requestId, status: r.status, in: tin, cached: tc, out: tout, note: "real 200 with zero cache" });
    }
    const hour = fmtTs(r.timestamp).slice(0, 5);
    const b = out.hourly[hour] ?? (out.hourly[hour] = { n: 0, input: 0, cached: 0, stub: 0 });
    b.n += 1; b.input += tin; b.cached += tc;
    if (tout <= 1) b.stub += 1;
  }
  return out;
}

function printHuman(rows, out, args) {
  const fmtWindow = (w, label) => {
    if (!w.n) { console.log(`${label}: no rows`); return; }
    console.log(`${label}: n=${w.n} (${w.errors} non-200/in=0) in=${fmt(w.input)} cached=${fmt(w.cached)} (${w.hitRate.toFixed(1)}%) out=${fmt(w.output)} | full-hit>=95%=${w.full} zero-cache-on-real=${w.zeroOnReal}`);
  };
  console.log(`session ${out.sessionId}: ${out.rows} usage rows | ${fmtTs(out.first)} -> ${fmtTs(out.last)}`);
  fmtWindow(out.head, `HEAD (first ${args.head})`);
  if (out.middle) fmtWindow(out.middle, "MIDDLE");
  fmtWindow(out.tail, `TAIL (last ${args.tail})`);
  fmtWindow(out.all, "ALL");
  if (rows.length > 0) {
    console.log("\nWarm-up (first 10):");
    rows.slice(0, 10).forEach((r, i) => {
      const u = r.usage ?? {};
      const tin = u.inputTokens || 0, tc = u.cachedInputTokens || 0;
      console.log(`  #${i + 1} ${fmtTs(r.timestamp)} in=${fmt(tin)} cached=${fmt(tc)} ${tin ? (100 * tc / tin).toFixed(1) : "-"}% out=${u.outputTokens ?? "-"} status=${r.status}`);
    });
  }
  if (out.anomalies.length) {
    console.log(`\nAnomalies (${out.anomalies.length}):`);
    for (const a of out.anomalies.slice(0, 20)) {
      console.log(`  ${fmtTs(a.ts)} ${a.requestId} status=${a.status} in=${fmt(a.in)} cached=${fmt(a.cached)} out=${a.out}${a.note ? ` [${a.note}]` : ""}`);
    }
  }
  console.log("\nHourly (hour: n / stub / hit%):");
  for (const [hour, b] of Object.entries(out.hourly).sort()) {
    console.log(`  ${hour} | n=${b.n} | stub=${b.stub} | ${b.input ? (100 * b.cached / b.input).toFixed(1) : "-"}%`);
  }
}

try {
  const args = parseArgs(process.argv.slice(2));
  args.usage = args.usage ? resolve(args.usage) : defaultUsagePath();
  let sessionId = args.session;
  if (!sessionId) {
    const hits = findSessionsByCwd(args.cwd);
    if (!hits.length) {
      console.error(`no codex session found with cwd containing "${args.cwd}"`);
      process.exit(2);
    }
    const allRows = readUsageRows(args.usage);
    const rowCount = new Map();
    for (const r of allRows) rowCount.set(r.conversationId, (rowCount.get(r.conversationId) ?? 0) + 1);
    const withRows = hits.map(h => ({ ...h, rows: rowCount.get(conversationIdFor(h.sessionId)) ?? 0 }))
      .filter(h => h.rows > 0);
    if (!withRows.length) {
      console.error(`no usage rows for any session with cwd containing "${args.cwd}"`);
      for (const h of hits) console.error(`  ${h.sessionId}  ${h.start}  ${h.cwd}`);
      process.exit(2);
    }
    console.error(`sessions with cwd ~ "${args.cwd}" (usage rows):`);
    for (const h of withRows) console.error(`  ${h.sessionId}  rows=${h.rows}  ${h.start}  ${h.cwd}`);
    withRows.sort((a, b) => b.rows - a.rows);
    sessionId = withRows[0].sessionId;
  }
  const cid = conversationIdFor(sessionId);
  const rows = readUsageRows(args.usage)
    .filter(r => r.conversationId === cid)
    .sort((a, b) => (a.timestamp ?? 0) - (b.timestamp ?? 0));
  if (!rows.length) {
    console.error(`no usage rows for ${sessionId} (digest ${cid}) in ${args.usage}`);
    process.exit(2);
  }
  const out = report(rows, { ...args, session: sessionId });
  if (args.json) console.log(JSON.stringify(out, null, 2));
  else printHuman(rows, out, args);
} catch (err) {
  console.error(`error: ${err.message}`);
  process.exit(2);
}

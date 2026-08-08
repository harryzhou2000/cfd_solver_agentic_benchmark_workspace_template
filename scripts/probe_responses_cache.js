#!/usr/bin/env node
/**
 * probe_responses_cache — repeated /v1/responses cache probe for official
 * DeepSeek (api.deepseek.com) and BLSC (LiteLLM proxy at llmapi.blsc.cn).
 *
 * Sends the same input N times to each target's /v1/responses endpoint and
 * prints, per request: HTTP status, LiteLLM entry id (BLSC), response id,
 * input/output/cached tokens, cached %, and output item types — so a human
 * can watch warm-up and spot entry/replica rotation.
 *
 * Options
 *   --count N       Identical requests per target (default 3)
 *   --input TEXT    Prompt to repeat (default "Reply with exactly: PONG";
 *                   note tiny prompts stay uncached — DeepSeek has a
 *                   minimum cacheable prefix, use --big for cache tests)
 *   --big           Prepend a ~5.4k-token stable system block so caching
 *                   actually engages
 *   --tool          Run one tool-call round-trip (get_current_time) per
 *                   target in addition to the repeated requests
 *   --targets LIST  comma-separated: blsc,deepseek (default: both)
 *   -h, --help      Show this help
 *
 * Credentials
 *   Keys read from ~/.opencodex/config.json (providers.BLSC.apiKey /
 *   providers.deepseek.apiKey) or $BLSC_KEY / $DEEPSEEK_KEY. Never printed.
 *
 * Notes
 *   - Non-streaming only: BLSC's streamed Responses usage is bare (no
 *     cached_tokens details); official DeepSeek reports details non-stream.
 *   - BLSC routes through LiteLLM model groups: requests can land on
 *     different entries/replicas, so a single run is not authoritative.
 */

const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const TARGETS = {
  blsc: { base: "https://llmapi.blsc.cn", model: "DeepSeek-V4-Flash", cfg: ["providers", "BLSC", "apiKey"], env: "BLSC_KEY" },
  deepseek: { base: "https://api.deepseek.com", model: "deepseek-v4-flash", cfg: ["providers", "deepseek", "apiKey"], env: "DEEPSEEK_KEY" },
};

const SYSTEM_BLOCK = ("You are Codex, an autonomous coding agent operating in a shared workspace. "
  + "You write production-quality code, verify your work with tests, and communicate with concise "
  + "commentary. Follow the repository instructions, never mutate remotes without authorization, "
  + "and checkpoint progress with commits. The workspace contains a CFD solver benchmark: inspect "
  + "the task specification, build the solver under solver/, run the required cases, and produce "
  + "the required artifacts and report. Validate outputs with the provided examiner. ")
  .repeat(90);

const TOOLS = [{
  type: "function",
  name: "get_current_time",
  description: "Returns the current wall-clock time in ISO 8601 format with milliseconds.",
  parameters: { type: "object", properties: {}, additionalProperties: false },
}];

const U = "What time is it right now? Call the get_current_time tool, then tell me the time you received.";

function parseArgs(argv) {
  const args = { count: 3, input: null, big: false, tool: false, targets: Object.keys(TARGETS), help: false };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--count") args.count = Number.parseInt(next(), 10);
    else if (a === "--input") args.input = next();
    else if (a === "--big") args.big = true;
    else if (a === "--tool") args.tool = true;
    else if (a === "--targets") args.targets = next().split(",").map(s => s.trim()).filter(Boolean);
    else if (a === "-h" || a === "--help") args.help = true;
    else throw new Error(`unknown argument "${a}"`);
  }
  for (const t of args.targets) if (!TARGETS[t]) throw new Error(`unknown target "${t}"`);
  return args;
}

function loadConfig() {
  try {
    return JSON.parse(fs.readFileSync(path.join(os.homedir(), ".opencodex/config.json"), "utf8"));
  } catch {
    return {};
  }
}

function keyFor(target, config) {
  let v = config;
  for (const k of target.cfg) {
    if (v == null) break;
    v = v[k];
  }
  if (typeof v === "string" && v.length) return v;
  return process.env[target.env] || null;
}

async function post(base, key, body) {
  const res = await fetch(`${base}/v1/responses`, {
    method: "POST",
    headers: { authorization: `Bearer ${key}`, "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const headers = {};
  res.headers.forEach((v, k) => { headers[k] = v; });
  const text = await res.text();
  let json = null;
  try { json = JSON.parse(text); } catch { /* non-JSON */ }
  return { status: res.status, headers, json, text };
}

function printRow(tag, r) {
  const j = r.json;
  if (!j) { console.log(`  ${tag}: HTTP ${r.status} (non-JSON) ${r.text.slice(0, 120)}`); return; }
  if (j.error) { console.log(`  ${tag}: HTTP ${r.status} ERROR ${JSON.stringify(j.error).slice(0, 200)}`); return; }
  const u = j.usage || {};
  const d = u.input_tokens_details || {};
  const cached = d.cached_tokens != null ? d.cached_tokens : "?";
  const pct = u.input_tokens ? (typeof cached === "number" ? (100 * cached / u.input_tokens).toFixed(1) : "?") : "?";
  const mid = String(r.headers["x-litellm-model-id"] || "").slice(0, 8) || "-";
  const items = (j.output || []).map(o => o.type).join(",") || "-";
  console.log(`  ${tag}: HTTP ${r.status} mid=${mid} id=${String(j.id || "").slice(0, 24)} in=${u.input_tokens} cached=${cached} (${pct}%) out=${u.output_tokens} items=[${items}]`);
}

function baseInput(input, big) {
  return big ? [{ role: "system", content: SYSTEM_BLOCK }, { role: "user", content: input }] : input;
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) {
    console.log(`probe_responses_cache [--count N] [--input TEXT] [--big] [--tool] [--targets blsc,deepseek] [-h]`);
    return;
  }
  const config = loadConfig();
  const input = args.input || "Reply with exactly: PONG";
  for (const name of args.targets) {
    const t = TARGETS[name];
    const key = keyFor(t, config);
    console.log(`\n== ${name} ${t.base}/v1/responses model=${t.model} count=${args.count} big=${args.big} tool=${args.tool} ==`);
    if (!key) { console.log("  no key found; skipping"); continue; }

    for (let i = 1; i <= args.count; i += 1) {
      const r = await post(t.base, key, { model: t.model, input: baseInput(input, args.big), stream: false });
      printRow(`req #${i}`, r);
    }

    if (args.tool) {
      const rA = await post(t.base, key, { model: t.model, input: baseInput(U, args.big), tools: TOOLS, stream: false });
      printRow("tool A", rA);
      const fc = (rA.json?.output || []).find(o => o.type === "function_call");
      if (rA.json?.error || !fc) {
        console.log(`  tool B: skipped (no function_call; error=${rA.json?.error?.message || "none"})`);
      } else {
        const history = [
          ...(args.big ? [{ role: "system", content: SYSTEM_BLOCK }] : []),
          { role: "user", content: U },
          // Responses-native assistant history: official DeepSeek rejects a
          // chat-style assistant message with tool_calls (400 "No tool call
          // found for tool output"); use a function_call item instead.
          { type: "function_call", call_id: fc.call_id, name: fc.name, arguments: fc.arguments },
          { type: "function_call_output", call_id: fc.call_id, output: new Date().toISOString() },
        ];
        const rB = await post(t.base, key, { model: t.model, input: history, tools: TOOLS, stream: false });
        printRow("tool B", rB);
      }
    }
  }
}

main().catch(e => { console.error(e.message || e); process.exit(1); });

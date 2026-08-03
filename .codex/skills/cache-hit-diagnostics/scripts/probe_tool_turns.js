#!/usr/bin/env node
/**
 * Tool-call growing-conversation cache probe.
 *
 * Each turn is two requests, mirroring the real agentic shape:
 *
 *   req A: [history..., U]            -> assistant tool_call (get_current_time)
 *   req B: [history..., U, A_tool, tool_result] -> assistant report
 *
 * U is the same every turn ("call get_current_time and report the time").
 * The tool result is the REAL current time (unique per turn). The assistant
 * tool-call message is replayed verbatim including reasoning_content, as
 * DeepSeek requires for tool-call turns.
 *
 * Usage:
 *   node scripts/probe_tool_turns.js [--steps 5] [--effort max] [--repeat N]
 *       [--targets blsc-flash,deepseek-flash]
 *
 * After the last turn, the final request body is resent --repeat times
 * (default 3; 0 disables) to measure the exact-match cache rate.
 */

const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const TARGETS = {
  "blsc-flash": { provider: "BLSC", model: "DeepSeek-V4-Flash" },
  "deepseek-flash": { provider: "deepseek", model: "deepseek-v4-flash" },
  "deepseek-pro": { provider: "deepseek", model: "deepseek-v4-pro" },
};

const U = "What time is it right now? Call the get_current_time tool, then tell me the time you received.";

const TOOLS = [{
  type: "function",
  function: {
    name: "get_current_time",
    description: "Returns the current wall-clock time in ISO 8601 format with milliseconds.",
    parameters: { type: "object", properties: {}, additionalProperties: false },
  },
}];

// Approximate the codex system/developer block (~15k tokens): a large stable
// prefix preceding the conversation, as in real agentic requests.
const SYSTEM_BLOCK = ("You are Codex, an autonomous coding agent operating in a shared workspace. "
  + "You write production-quality code, verify your work with tests, and communicate with concise "
  + "commentary. Follow the repository instructions, never mutate remotes without authorization, "
  + "and checkpoint progress with commits. The workspace contains a CFD solver benchmark: inspect "
  + "the task specification, build the solver under solver/, run the required cases, and produce "
  + "the required artifacts and report. Validate outputs with the provided examiner. ")
  .repeat(120);

function printHelp() {
  console.log(`
probe_tool_turns — prompt-cache probe for DeepSeek-compatible chat/completions

What it does
  Reproduces the request shape of a coding agent that calls tools, and prints
  what each response reports about token usage and prompt caching. Every turn
  is two requests, just like a real agentic workflow:

      request A: the user asks for the time   ->  the model calls get_current_time
      request B: history + the tool result    ->  the model reports the time

  The conversation grows each turn and all previous turns are re-sent verbatim
  (including the model's own reasoning_content), so a healthy prefix cache
  should show rising "cached" numbers. After the last turn, the final request
  is re-sent a few times to measure exact-match caching.

Usage
  node scripts/probe_tool_turns.js [options]

Options
  --steps N        Number of tool turns to run (default 5)
  --effort NAME    reasoning_effort to send: none|low|medium|high|xhigh|max
                   (default max)
  --repeat N       Exact re-sends of the final request (default 3, 0 disables)
  --stream         Use the streaming (SSE) path, like production agents do
  --system         Prepend a ~15k-token system message to approximate a coding
                   agent's system/developer block
  --targets LIST   Providers to test, comma-separated: blsc-flash, deepseek-flash
                   (default: both)
  -h, --help       Show this help

Credentials
  Keys are read from ~/.opencodex/config.json (or $OPENCODEX_HOME/config.json):
  providers.BLSC.apiKey / apiKeyPool and providers.deepseek.apiKey. If that
  config is missing, the BLSC_KEY and DEEPSEEK_KEY environment variables are
  used instead. Keys are passed to curl via the environment and are never
  printed or written to disk.

Requirements
  Node.js (any recent version, no dependencies) and curl on PATH.

What to look for
  Healthy cache: "cached" grows with the conversation and the final exact
  re-sends land near 100%. Suspicious: streaming responses that always report
  cached=0 and completion_tokens=1 while the non-streaming path reports real
  numbers for the same request — that indicates the streaming endpoint is not
  reporting usage honestly (a stub usage chunk), not that the cache is empty.

Example output (real capture, BLSC flash, --steps 1 --repeat 1 --stream):

  == BLSC DeepSeek-V4-Flash (https://llmapi.blsc.cn/chat/completions) — 1 tool turns, effort=max, stream=true, system=false ==
    turn 1 A(tool): in=30 cached=0 (0.0%) out=1 | calls=get_current_time
    turn 1 B(report): in=113 cached=0 (0.0%) out=1 | "The current time is **2026-08-02 13:24:29 UTC** (specificall"
    repeat #1: in=113 cached=0 (0.0%) out=1 [stop]

  Note that "in" is the stub's token count (the real prompt carries a ~200-token
  tools schema plus the user message), and even the exact re-send of the final
  body reports cached=0 out=1. The same turns without --stream report real
  accounting (e.g. turn 1 B: in=454 cached=384 out=48), so the cache exists —
  only the streaming usage chunk is stubbed.
`);
}

function parseArgs(argv) {
  const args = { steps: 5, effort: "max", repeat: 3, stream: false, system: false, targets: Object.keys(TARGETS) };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--steps") args.steps = Number.parseInt(next(), 10);
    else if (a === "--effort") args.effort = next();
    else if (a === "--repeat") args.repeat = Number.parseInt(next(), 10);
    else if (a === "--stream") args.stream = true;
    else if (a === "--system") args.system = true;
    else if (a === "--targets") args.targets = next().split(",").map(s => s.trim()).filter(Boolean);
    else if (a === "--help" || a === "-h") {
      printHelp();
      process.exit(0);
    } else throw new Error(`unknown argument "${a}"`);
  }
  for (const t of args.targets) if (!TARGETS[t]) throw new Error(`unknown target "${t}"`);
  return args;
}

function loadOcxConfig() {
  const home = process.env.OPENCODEX_HOME || path.join(os.homedir(), ".opencodex");
  try {
    return JSON.parse(fs.readFileSync(path.join(home, "config.json"), "utf8"));
  } catch {
    return {};
  }
}

function providerKeys(config, providerName) {
  const provider = config.providers?.[providerName];
  const keys = [
    provider?.apiKey,
    ...(Array.isArray(provider?.apiKeyPool)
      ? provider.apiKeyPool.map(entry => typeof entry === "string" ? entry : entry?.key)
      : []),
  ].filter(key => typeof key === "string" && key.length > 0);
  if (!keys.length) {
    const envKey = providerName === "BLSC" ? process.env.BLSC_KEY : process.env.DEEPSEEK_KEY;
    if (envKey) keys.push(envKey);
  }
  return [...new Set(keys)];
}

function curlOnce(url, key, body) {
  const out = execFileSync(
    "bash",
    ["-c", 'curl -s -w "\\n%{http_code}" -X POST "$PROBE_URL" -H "Authorization: Bearer $PROBE_KEY" -H "Content-Type: application/json" -d @-'],
    {
      input: body,
      env: { ...process.env, PROBE_URL: url, PROBE_KEY: key },
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
      stdio: ["pipe", "pipe", "ignore"],
    },
  );
  const sep = out.lastIndexOf("\n");
  return { http: out.slice(sep + 1).trim(), text: out.slice(0, sep) };
}

function parseResponse(text) {
  // SSE streaming: accumulate deltas (content, reasoning_content, tool_calls)
  // and take usage from the tail chunk.
  if (text.includes("data:")) {
    let content = "", reasoning = "", finish = null, usage = null;
    const toolCalls = {};
    for (const line of text.split("\n")) {
      if (!line.startsWith("data:")) continue;
      const payload = line.slice(5).trim();
      if (payload === "[DONE]") continue;
      let j;
      try { j = JSON.parse(payload); } catch { continue; }
      if (j.usage) usage = j.usage;
      const delta = j.choices?.[0]?.delta;
      if (delta) {
        if (typeof delta.content === "string") content += delta.content;
        if (typeof delta.reasoning_content === "string") reasoning += delta.reasoning_content;
        if (j.choices?.[0]?.finish_reason) finish = j.choices[0].finish_reason;
        if (Array.isArray(delta.tool_calls)) {
          for (const tc of delta.tool_calls) {
            const idx = tc.index ?? 0;
            const acc = toolCalls[idx] || (toolCalls[idx] = { id: "", type: "function", function: { name: "", arguments: "" } });
            if (tc.id) acc.id += tc.id;
            if (tc.function?.name) acc.function.name += tc.function.name;
            if (tc.function?.arguments) acc.function.arguments += tc.function.arguments;
          }
        }
      }
    }
    if (usage) {
      const details = usage.prompt_tokens_details || {};
      return {
        input: usage.prompt_tokens,
        cached: details.cached_tokens ?? usage.cached_tokens ?? 0,
        output: usage.completion_tokens,
        content,
        reasoning,
        toolCalls: Object.values(toolCalls).filter(t => t.function.name),
        finish,
      };
    }
    return { raw: text.slice(0, 200) };
  }
  try {
    const json = JSON.parse(text);
    if (json.error) return { error: json.error.message };
    const usage = json.usage || {};
    const details = usage.prompt_tokens_details || {};
    const msg = json.choices?.[0]?.message || {};
    return {
      input: usage.prompt_tokens,
      cached: details.cached_tokens ?? usage.cached_tokens ?? 0,
      output: usage.completion_tokens,
      content: typeof msg.content === "string" ? msg.content : "",
      reasoning: typeof msg.reasoning_content === "string" ? msg.reasoning_content : "",
      toolCalls: Array.isArray(msg.tool_calls) ? msg.tool_calls : [],
      finish: json.choices?.[0]?.finish_reason,
    };
  } catch {
    return { raw: text.slice(0, 200) };
  }
}

function bodyFor(model, history, effort, tail, stream, system) {
  // history: array of already-serialized message objects.
  // tail: array of message objects for the current turn (e.g. [U] or [U, A_tool, tool_result]).
  const messages = [...history, ...tail];
  if (system) messages.unshift({ role: "system", content: SYSTEM_BLOCK });
  const body = { model, messages, tools: TOOLS, stream: !!stream };
  if (stream) body.stream_options = { include_usage: true };
  if (effort) body.reasoning_effort = effort;
  return JSON.stringify(body);
}

function runTarget(target, args) {
  const config = loadOcxConfig();
  const provider = config.providers?.[target.provider] || {};
  const base = provider.baseUrl || (target.provider === "BLSC" ? "https://llmapi.blsc.cn" : "https://api.deepseek.com");
  const url = `${base.replace(/\/+$/, "")}/chat/completions`;
  const keys = providerKeys(config, target.provider);
  console.log(`\n== ${target.provider} ${target.model}  (${url}) — ${args.steps} tool turns, effort=${args.effort}, stream=${args.stream}, system=${args.system} ==`);

  const history = [];
  let usedKey = null;
  for (const key of keys) {
    let ok = true;
    const probe = bodyFor(target.model, [], args.effort, [{ role: "user", content: U }], args.stream, args.system);
    const { http } = curlOnce(url, key, probe);
    if (http === "401") { ok = false; console.log(`  key ${key.slice(0, 8)}… rejected (401)`); }
    if (ok) { usedKey = key; break; }
  }
  if (!usedKey) { console.log("  no usable key"); return; }

  let lastBody = null;
  for (let step = 1; step <= args.steps; step += 1) {
    // req A: model must emit the tool call
    const bodyA = bodyFor(target.model, history, args.effort, [{ role: "user", content: U }], args.stream, args.system);
    const { http: httpA, text: textA } = curlOnce(url, usedKey, bodyA);
    const a = parseResponse(textA);
    if (a.error || a.raw) { console.log(`  turn ${step} reqA: HTTP ${httpA} ${a.error ?? a.raw}`); return; }
    const pctA = a.input ? (100 * a.cached / a.input).toFixed(1) : "0.0";
    console.log(`  turn ${step} A(tool): in=${a.input} cached=${a.cached} (${pctA}%) out=${a.output} | calls=${a.toolCalls.map(c => c.function?.name).join(",") || "NONE"}`);

    const toolCall = a.toolCalls[0] || { id: "call_fallback", function: { name: "get_current_time", arguments: "{}" } };
    const toolResult = { role: "tool", tool_call_id: toolCall.id, content: new Date().toISOString() };
    const asstToolMsg = { role: "assistant", content: a.content };
    if (a.reasoning) asstToolMsg.reasoning_content = a.reasoning;
    if (a.toolCalls.length) asstToolMsg.tool_calls = a.toolCalls;

    // req B: model must report the time
    const tailB = [{ role: "user", content: U }, asstToolMsg, toolResult];
    const bodyB = bodyFor(target.model, history, args.effort, tailB, args.stream, args.system);
    lastBody = bodyB;
    const { http: httpB, text: textB } = curlOnce(url, usedKey, bodyB);
    const b = parseResponse(textB);
    if (b.error || b.raw) { console.log(`  turn ${step} reqB: HTTP ${httpB} ${b.error ?? b.raw}`); return; }
    const pctB = b.input ? (100 * b.cached / b.input).toFixed(1) : "0.0";
    const report = b.content.replace(/\s+/g, " ").trim().slice(0, 60) || "(empty)";
    console.log(`  turn ${step} B(report): in=${b.input} cached=${b.cached} (${pctB}%) out=${b.output} | "${report}"`);

    history.push(
      { role: "user", content: U },
      asstToolMsg,
      toolResult,
      { role: "assistant", content: b.content, ...(b.reasoning ? { reasoning_content: b.reasoning } : {}) },
    );
  }

  // exact resends of the final req-B body
  for (let k = 1; k <= args.repeat; k += 1) {
    const { http, text } = curlOnce(url, usedKey, lastBody);
    const r = parseResponse(text);
    if (r.error || r.raw) { console.log(`  repeat #${k}: HTTP ${http} ${r.error ?? r.raw}`); return; }
    const pct = r.input ? (100 * r.cached / r.input).toFixed(1) : "0.0";
    console.log(`  repeat #${k}: in=${r.input} cached=${r.cached} (${pct}%) out=${r.output} [${r.finish}]`);
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  for (const t of args.targets) runTarget(TARGETS[t], args);
}

main();

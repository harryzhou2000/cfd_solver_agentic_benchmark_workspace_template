#!/usr/bin/env node
/**
 * Growing-conversation cache probe with REAL model responses.
 *
 * Builds an escalating conversation and reuses the model's actual output as
 * the assistant history of the next request:
 *
 *   step 1: [U1]                     -> A1
 *   step 2: [U1, A1, U2]             -> A2
 *   step 3: [U1, A1, U2, A2, U3]     -> A3
 *   ...  (every UN is the same text)
 *
 * The final step's exact body is then resent --repeat times (default 3;
 * 0 disables) to measure the exact-match cache rate. Assistant turns are
 * copied verbatim from the previous response, including `reasoning_content`
 * when the provider returns it, so the cache is exercised on genuinely
 * model-generated context.
 *
 * Credentials come from the ocx config (~/.opencodex/config.json or
 * $OPENCODEX_HOME/config.json): providers.BLSC.apiKey / apiKeyPool and
 * providers.deepseek.apiKey. Keys are passed via the environment, never
 * printed.
 *
 * Usage:
 *   node scripts/probe_real_turns.js [--steps 5] [--max-tokens N]
 *       [--effort max] [--repeat N] [--shuffle] [--seed N]
 *       [--targets blsc-flash,deepseek-flash]
 *
 * By default no max_tokens is sent (no output limit); pass --max-tokens N to
 * cap it. The user message U is a ~700-token block, identical every turn.
 * With --shuffle, every UN is a fresh random permutation of a larger ~3k-token
 * block set (seeded by --seed, default 42), so consecutive turns share no
 * user-message content.
 */

const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const TARGETS = {
  "blsc-flash": { provider: "BLSC", model: "DeepSeek-V4-Flash" },
  "deepseek-flash": { provider: "deepseek", model: "deepseek-v4-flash" },
};

const U = ("The quick brown fox jumps over the lazy dog. Pack my box with five dozen liquor jugs. "
  + "How vexingly quick daft zebras jump! Sphinx of black quartz, judge my vow. ").repeat(35)
  + "What is the capital of France? Reply in one short sentence.";

const SHUFFLE_BLOCKS = [
  "The quick brown fox jumps over the lazy dog.",
  "Pack my box with five dozen liquor jugs.",
  "How vexingly quick daft zebras jump!",
  "Sphinx of black quartz, judge my vow.",
  "The five boxing wizards jump quickly.",
  "Jackdaws love my big sphinx of quartz.",
  "Bright vixens jump; dozy fowl quack.",
  "Glib jocks quiz nymphs to vex dwarf.",
  "Waltz, bad nymph, for quick jigs vex.",
  "Cozy lummox gives smart squid who asks for job pen.",
  "A quick brown fox jumps over the lazy dog near the riverbank.",
  "Two driven jocks help fax my big quiz.",
];

// mulberry32 seeded PRNG so runs are reproducible.
function rng(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function shuffledU(rand, passes) {
  const arr = [];
  for (let p = 0; p < passes; p += 1) arr.push(...SHUFFLE_BLOCKS);
  for (let i = arr.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rand() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr.join(" ") + " What is the capital of France? Reply in one short sentence.";
}

function parseArgs(argv) {
  const args = { steps: 5, maxTokens: null, effort: "max", repeat: 3, shuffle: false, seed: 42, targets: Object.keys(TARGETS) };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--steps") args.steps = Number.parseInt(next(), 10);
    else if (a === "--max-tokens") args.maxTokens = Number.parseInt(next(), 10);
    else if (a === "--effort") args.effort = next();
    else if (a === "--repeat") args.repeat = Number.parseInt(next(), 10);
    else if (a === "--no-repeat") args.repeat = 0;
    else if (a === "--shuffle") args.shuffle = true;
    else if (a === "--seed") args.seed = Number.parseInt(next(), 10);
    else if (a === "--targets") args.targets = next().split(",").map(s => s.trim()).filter(Boolean);
    else if (a === "--help" || a === "-h") {
      console.log(`Usage: node scripts/probe_real_turns.js [--steps N] [--max-tokens N] [--effort NAME] [--repeat N] [--shuffle] [--seed N] [--targets ${Object.keys(TARGETS).join(",")}] (repeat defaults to 3, 0 disables; no max_tokens by default)`);
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
      finish: json.choices?.[0]?.finish_reason,
    };
  } catch {
    return { raw: text.slice(0, 200) };
  }
}

function bodyFor(model, history, effort, maxTokens, newUserText, stream) {
  const messages = [];
  for (const pair of history) {
    messages.push({ role: "user", content: pair.user });
    const m = { role: "assistant", content: pair.assistant.content };
    if (pair.assistant.reasoning) m.reasoning_content = pair.assistant.reasoning;
    messages.push(m);
  }
  messages.push({ role: "user", content: newUserText });
  const body = { model, messages, stream: !!stream };
  if (maxTokens) body.max_tokens = maxTokens;
  if (effort) body.reasoning_effort = effort;
  return JSON.stringify(body);
}

function runTarget(target, args) {
  const config = loadOcxConfig();
  const provider = config.providers?.[target.provider] || {};
  const base = provider.baseUrl || (target.provider === "BLSC" ? "https://llmapi.blsc.cn" : "https://api.deepseek.com");
  const url = `${base.replace(/\/+$/, "")}/chat/completions`;
  const keys = providerKeys(config, target.provider);
  const rand = rng(args.seed);
  const userText = args.shuffle ? () => shuffledU(rand, 60) : () => U;
  console.log(`\n== ${target.provider} ${target.model}  (${url}) — ${args.steps} growing steps, effort=${args.effort}, shuffle=${args.shuffle} seed=${args.seed} ==`);

  const history = [];
  let usedKey = null;
  for (const key of keys) {
    let ok = true;
    const probe = bodyFor(target.model, [], args.effort, args.maxTokens, userText());
    const { http } = curlOnce(url, key, probe);
    if (http === "401") { ok = false; console.log(`  key ${key.slice(0, 8)}… rejected (401)`); }
    if (ok) { usedKey = key; break; }
  }
  if (!usedKey) { console.log("  no usable key"); return; }

  let lastBody = null;
  for (let step = 1; step <= args.steps; step += 1) {
    const newU = userText();
    const body = bodyFor(target.model, history, args.effort, args.maxTokens, newU, args.stream);
    lastBody = body;
    const { http, text } = curlOnce(url, usedKey, body);
    const r = parseResponse(text);
    if (r.error || r.raw) { console.log(`  step ${step}: HTTP ${http} ${r.error ?? r.raw}`); return; }
    const pct = r.input ? (100 * r.cached / r.input).toFixed(1) : "0.0";
    const aText = r.content.replace(/\s+/g, " ").trim().slice(0, 70) || "(empty content)";
    console.log(`  step ${step}: in=${r.input} cached=${r.cached} (${pct}%) out=${r.output} | A${step}: "${aText}" [reasoning ${r.reasoning.length} chars, ${r.finish}]`);
    history.push({ user: newU, assistant: { content: r.content, reasoning: r.reasoning } });
  }

  for (let k = 1; k <= args.repeat; k += 1) {
    const body = lastBody;
    const { http, text } = curlOnce(url, usedKey, body);
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

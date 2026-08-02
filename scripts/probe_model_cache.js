#!/usr/bin/env node
/**
 * Prompt-cache probe for BLSC DeepSeek models vs the official DeepSeek API.
 *
 * Sends the same request N times in a row (default 5) for each target model and
 * prints per-request usage (input / output / cached tokens) so a working prefix
 * cache shows as: request 1 miss, requests 2..N hit with cached ≈ input.
 * By default the request carries `reasoning_effort: "max"` and a previous
 * assistant turn with `reasoning_content` in the history (the real agentic wire
 * shape, minus tool calls); pass `--effort none` / `--no-reasoning` for the
 * plain-text shape. `--turns N` builds an N-turn conversation (user/assistant
 * pairs, assistant turns carry reasoning_content) instead of a single user turn.
 *
 * Credentials are read automatically from the ocx config
 * (~/.opencodex/config.json, or $OPENCODEX_HOME/config.json):
 *   - providers.BLSC.apiKey + providers.BLSC.apiKeyPool (tried in order)
 *   - providers.deepseek.apiKey
 * Keys are passed to curl via the environment and are never printed.
 *
 * Usage:
 *   node scripts/probe_model_cache.js [--count 5] [--repeat 120] [--max-tokens 8]
 *       [--effort none|low|medium|high|xhigh|max] [--turns 1] [--reasoning-repeat 20]
 *       [--no-reasoning] [--targets blsc-flash,deepseek-flash]
 *
 * Targets:
 *   blsc-flash      BLSC    DeepSeek-V4-Flash
 *   deepseek-flash  deepseek deepseek-v4-flash (official api.deepseek.com)
 */

const { execFileSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const TARGETS = {
  "blsc-flash": { provider: "BLSC", model: "DeepSeek-V4-Flash" },
  "deepseek-flash": { provider: "deepseek", model: "deepseek-v4-flash" },
};

const SENTENCE = "The quick brown fox jumps over the lazy dog. ";
const REASONING_SENTENCE = "Let me think through this carefully step by step. ";

function parseArgs(argv) {
  const args = {
    count: 5,
    repeat: 120,
    maxTokens: 8,
    maxTokensExplicit: false,
    effort: "max",
    turns: 1,
    reasoningRepeat: 20,
    reasoning: true,
    targets: Object.keys(TARGETS),
  };
  const EFFORTS = new Set(["none", "low", "medium", "high", "xhigh", "max"]);
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--count") args.count = Number.parseInt(next(), 10);
    else if (a === "--repeat") args.repeat = Number.parseInt(next(), 10);
    else if (a === "--max-tokens") {
      args.maxTokens = Number.parseInt(next(), 10);
      args.maxTokensExplicit = true;
    } else if (a === "--effort") {
      const value = next();
      if (!EFFORTS.has(value)) throw new Error(`unknown effort "${value}"; valid: none, low, medium, high, xhigh, max`);
      args.effort = value;
    } else if (a === "--turns") {
      args.turns = Number.parseInt(next(), 10);
    } else if (a === "--reasoning-repeat") {
      args.reasoningRepeat = Number.parseInt(next(), 10);
    } else if (a === "--no-reasoning") {
      args.reasoning = false;
    } else if (a === "--dry-run") {
      args.dryRun = true;
    } else if (a === "--targets") {
      args.targets = next().split(",").map(s => s.trim()).filter(Boolean);
      for (const t of args.targets) {
        if (!TARGETS[t]) throw new Error(`unknown target "${t}"; valid: ${Object.keys(TARGETS).join(", ")}`);
      }
    } else if (a === "--help" || a === "-h") {
      console.log(`Usage: node scripts/probe_model_cache.js [--count N] [--repeat N] [--max-tokens N] [--effort none|low|medium|high|xhigh|max] [--turns N] [--reasoning-repeat N] [--no-reasoning] [--targets ${Object.keys(TARGETS).join(",")}] [--dry-run]`);
      process.exit(0);
    } else {
      throw new Error(`unknown argument "${a}"`);
    }
  }
  // Reasoning turns need an output budget; raise the default so max effort is
  // not truncated by the tiny plain-text default.
  if (!args.maxTokensExplicit && args.effort !== "none" && args.maxTokens < 256) {
    args.maxTokens = 256;
  }
  return args;
}

function loadOcxConfig() {
  const home = process.env.OPENCODEX_HOME || path.join(os.homedir(), ".opencodex");
  const configPath = path.join(home, "config.json");
  try {
    return JSON.parse(fs.readFileSync(configPath, "utf8"));
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

const TURN_QUESTIONS = [
  "What is the capital of France?",
  "What about Germany?",
  "And Spain?",
  "And Italy?",
  "And Portugal?",
];

function buildBody(model, repeat, maxTokens, effort, reasoning, reasoningRepeat, turns) {
  const prefix = SENTENCE.repeat(repeat);
  const messages = [];
  const turnCount = Math.max(1, turns);
  if (turnCount === 1 && reasoning) {
    // Preserve the validated single-turn shape: long user turn, one assistant
    // reasoning turn, then the final user question.
    messages.push({ role: "user", content: `${prefix}${TURN_QUESTIONS[0]}` });
    messages.push({
      role: "assistant",
      content: "I should reason before answering.",
      reasoning_content: REASONING_SENTENCE.repeat(reasoningRepeat),
    });
    messages.push({ role: "user", content: "The capital of France is Paris" });
  } else {
    // N user turns with an assistant reasoning turn between them; the last
    // message is a user question (no tool calls yet).
    for (let t = 1; t <= turnCount; t += 1) {
      messages.push({
        role: "user",
        content: t === 1
          ? `${prefix}${TURN_QUESTIONS[0]}`
          : TURN_QUESTIONS[(t - 1) % TURN_QUESTIONS.length],
      });
      if (t < turnCount) {
        messages.push({
          role: "assistant",
          content: "I should reason before answering.",
          ...(reasoning ? { reasoning_content: REASONING_SENTENCE.repeat(reasoningRepeat) } : {}),
        });
      }
    }
  }
  const body = {
    model,
    messages,
    max_tokens: maxTokens,
    stream: false,
  };
  if (effort && effort !== "none") body.reasoning_effort = effort;
  return JSON.stringify(body);
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

function parseUsage(text) {
  try {
    const json = JSON.parse(text);
    if (json.error) return { error: json.error.message, code: json.error.code };
    const usage = json.usage || {};
    const details = usage.prompt_tokens_details || {};
    return {
      model: json.model,
      input: usage.prompt_tokens,
      output: usage.completion_tokens,
      cached: details.cached_tokens ?? usage.prompt_cache_hit_tokens ?? usage.cached_tokens,
      cacheHit: usage.prompt_cache_hit_tokens,
      cacheMiss: usage.prompt_cache_miss_tokens,
      details,
    };
  } catch {
    return { raw: text.slice(0, 200) };
  }
}

function fmt(v) {
  return v === undefined || v === null ? "—" : String(v);
}

function probeTarget(target, args) {
  const config = loadOcxConfig();
  const provider = config.providers?.[target.provider] || {};
  const base = provider.baseUrl || (target.provider === "BLSC" ? "https://llmapi.blsc.cn" : "https://api.deepseek.com");
  const url = `${base.replace(/\/+$/, "")}/chat/completions`;
  const body = buildBody(
    target.model,
    args.repeat,
    args.maxTokens,
    args.effort,
    args.reasoning,
    args.reasoningRepeat,
    args.turns,
  );
  const keys = providerKeys(config, target.provider);

  if (args.dryRun) {
    console.log(`\n== ${target.provider} ${target.model} — dry run (body not sent) ==`);
    console.log(body);
    return;
  }

  console.log(
    `\n== ${target.provider} ${target.model}  (${url}) — ${args.count} consecutive identical requests`
    + `, reasoning_effort=${args.effort}, turns=${args.turns}`
    + `, reasoning_content=${args.reasoning ? `${args.reasoningRepeat} repeats` : "off"} ==`,
  );

  let usedKey = null;
  let results = null;
  for (const key of keys) {
    results = [];
    let authOk = true;
    for (let i = 1; i <= args.count; i += 1) {
      const { http, text } = curlOnce(url, key, body);
      const parsed = parseUsage(text);
      results.push({ http, parsed });
      if (http === "401" && i === 1) {
        authOk = false;
        break;
      }
    }
    if (authOk) {
      usedKey = key;
      break;
    }
    console.log(`  key ${key.slice(0, 8)}… rejected (401): ${results[0]?.parsed?.error ?? "unauthorized"}`);
  }
  if (!usedKey) {
    console.log("  no usable key in the provider pool");
    return;
  }

  results.forEach((r, i) => {
    const p = r.parsed;
    if (p.error || p.raw) {
      console.log(`  #${i + 1}  HTTP ${r.http}  ${p.error ?? p.raw}`);
      return;
    }
    console.log(
      `  #${i + 1}  HTTP ${r.http}  input=${fmt(p.input)}  output=${fmt(p.output)}`
      + `  cached=${fmt(p.cached)}${p.cacheHit !== undefined ? `  hit=${fmt(p.cacheHit)}` : ""}`
      + `${p.cacheMiss !== undefined ? `  miss=${fmt(p.cacheMiss)}` : ""}`
      + `${p.details && Object.keys(p.details).length ? `  details=${JSON.stringify(p.details)}` : ""}`,
    );
  });

  const hits = results.filter(r => r.parsed && typeof r.parsed.cached === "number" && r.parsed.cached > 0);
  const inputs = results.map(r => r.parsed?.input).filter(v => typeof v === "number");
  const totalInput = inputs.reduce((a, b) => a + b, 0);
  const totalCached = results
    .map(r => (typeof r.parsed?.cached === "number" ? r.parsed.cached : 0))
    .reduce((a, b) => a + b, 0);
  console.log(
    `  => ${hits.length}/${results.length} requests with cached>0`
    + `  total input=${totalInput}  total cached=${totalCached}`
    + `  cache rate=${totalInput > 0 ? `${(100 * totalCached / totalInput).toFixed(1)}%` : "n/a"}`,
  );
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  for (const name of args.targets) probeTarget(TARGETS[name], args);
}

main();

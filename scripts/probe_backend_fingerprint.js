#!/usr/bin/env node
/**
 * Backend fingerprint probe for an OpenAI-compatible gateway fronted by
 * LiteLLM (e.g. BLSC llmapi.blsc.cn).
 *
 * What it does
 *   Sends repeated tiny chat-completion requests to each model group and
 *   classifies, per request, the envelope family it came from:
 *
 *     - x-litellm-model-id      (LiteLLM model-group entry; changes => the
 *                                router switched upstreams between requests)
 *     - response id style       (chatcmpl-<ULID>, bare uuid4, cmpl-<hex32>,
 *                                chatcmpl-<uuid4>, ...)
 *     - llm_provider-x-request-id / x-trace-id styles
 *     - system_fingerprint      (vllm-... / fp_... / none)
 *     - llm_provider-server     (uvicorn => classic vLLM, etc.)
 *     - usage details keys      (prompt_tokens_details / completion_tokens_details)
 *     - message provider_specific_fields keys
 *     - tool-call id format     (call_00_..., chatcmpl-tool-<16hex>, ...)
 *
 *   Optionally probes the Anthropic-compatible /v1/messages and OpenAI
 *   Responses /v1/responses surfaces for each model.
 *
 * Why repeated samples
 *   A LiteLLM "model group" can contain several upstream entries (replicas,
 *   fallbacks, different engines). A single request only shows the entry the
 *   router picked that time; the envelope can change between requests. Sample
 *   repeatedly and key every observation by x-litellm-model-id.
 *
 * Usage
 *   node scripts/probe_backend_fingerprint.js [--models Kimi-K3,DeepSeek-V4-Flash]
 *       [--samples 6] [--tool] [--surfaces] [-h]
 *
 * Credentials
 *   Key read from ~/.opencodex/config.json providers.BLSC.apiKey (or
 *   $BLSC_KEY). Never printed.
 */

const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const DEFAULT_BASE = "https://llmapi.blsc.cn";
const TOOLS = [{
  type: "function",
  function: {
    name: "get_current_time",
    description: "Returns the current wall-clock time in ISO 8601 format with milliseconds.",
    parameters: { type: "object", properties: {}, additionalProperties: false },
  },
}];

function parseArgs(argv) {
  const args = { models: null, samples: 6, tool: false, surfaces: false, help: false };
  for (let i = 0; i < argv.length; i += 1) {
    const a = argv[i];
    const next = () => argv[++i];
    if (a === "--models") args.models = next().split(",").map(s => s.trim()).filter(Boolean);
    else if (a === "--samples") args.samples = Number.parseInt(next(), 10);
    else if (a === "--tool") args.tool = true;
    else if (a === "--surfaces") args.surfaces = true;
    else if (a === "-h" || a === "--help") args.help = true;
    else throw new Error(`unknown argument "${a}"`);
  }
  if (!args.models || !args.models.length) args.models = ["Kimi-K3", "DeepSeek-V4-Flash"];
  return args;
}

function loadKey() {
  try {
    const cfg = JSON.parse(fs.readFileSync(path.join(os.homedir(), ".opencodex/config.json"), "utf8"));
    const k = cfg.providers?.BLSC?.apiKey;
    if (k) return k;
  } catch { /* fall through to env */ }
  if (process.env.BLSC_KEY) return process.env.BLSC_KEY;
  throw new Error("no BLSC key found in ~/.opencodex/config.json or $BLSC_KEY");
}

function idStyle(id) {
  if (!id) return "none";
  const s = String(id);
  if (/^chatcmpl-[0-9A-Z]{26}$/.test(s)) return "chatcmpl-ULID26";
  if (/^chatcmpl-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(s)) return "chatcmpl-uuid4";
  if (/^chatcmpl-[0-9a-f]{32}$/.test(s)) return "chatcmpl-hex32";
  if (/^cmpl-[0-9a-f]{32}$/.test(s)) return "cmpl-hex32";
  if (/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(s)) return "bare-uuid4";
  if (/^[0-9a-f]{32}$/.test(s)) return "bare-hex32";
  if (/^resp_/.test(s)) return "resp_<" + s.length + ">";
  return "other:" + s.slice(0, 32);
}

async function post(base, key, urlPath, body, headers = {}) {
  const res = await fetch(`${base}${urlPath}`, {
    method: "POST",
    headers: { authorization: `Bearer ${key}`, "content-type": "application/json", ...headers },
    body: JSON.stringify(body),
  });
  const h = {};
  res.headers.forEach((v, k) => { h[k] = v; });
  const text = await res.text();
  let j = null;
  try { j = JSON.parse(text); } catch { /* non-JSON body */ }
  return { status: res.status, h, text, j };
}

function chatRow(r) {
  const usage = r.j?.usage || {};
  const msg = r.j?.choices?.[0]?.message || {};
  const psf = msg.provider_specific_fields || {};
  const tids = (msg.tool_calls || []).map(t => String(t.id || "").slice(0, 36));
  const ptd = usage.prompt_tokens_details ? Object.keys(usage.prompt_tokens_details).join("+") : "-";
  const ctd = usage.completion_tokens_details ? Object.keys(usage.completion_tokens_details).join("+") : "-";
  return {
    status: r.status,
    mid: (r.h["x-litellm-model-id"] || "").slice(0, 8),
    id: idStyle(r.j?.id),
    fp: r.j?.system_fingerprint || "",
    server: r.h["llm_provider-server"] || "-",
    reqid: idStyle(r.h["llm_provider-x-request-id"]),
    usageKeys: Object.keys(usage).join("+"),
    ptd, ctd,
    psf: Object.keys(psf).join("+"),
    tids: tids.join("|"),
    err: r.j?.error?.message ? r.j.error.message.slice(0, 110) : "",
  };
}

function printChatRow(label, r) {
  const x = chatRow(r);
  console.log(`  ${label} st=${x.status} mid=${x.mid} id=${x.id} fp=${x.fp || "n"} srv=${x.server} reqid=${x.reqid} usage[${x.usageKeys}] ptd[${x.ptd}] ctd[${x.ctd}] psf[${x.psf}]${x.tids ? " tids=" + x.tids : ""}${x.err ? " err=" + x.err : ""}`);
}

async function main() {
  const args = parseArgs(process.argv.slice(2));
  if (args.help) {
    console.log(`probe_backend_fingerprint [--models A,B] [--samples N] [--tool] [--surfaces] [-h]`);
    return;
  }
  const key = loadKey();
  const base = DEFAULT_BASE;
  console.log(`== backend fingerprint probe @ ${base} ==`);
  for (const model of args.models) {
    console.log(`\n== ${model} ==`);
    for (let i = 1; i <= args.samples; i += 1) {
      const r = await post(base, key, "/v1/chat/completions", {
        model, messages: [{ role: "user", content: "Reply with exactly: PONG" }], max_tokens: 8,
      });
      printChatRow(`#${i}`, r);
    }
    if (args.tool) {
      const r = await post(base, key, "/v1/chat/completions", {
        model, messages: [{ role: "user", content: "call get_current_time and report the time" }],
        tools: TOOLS, max_tokens: 2000,
      });
      printChatRow("tool", r);
    }
    if (args.surfaces) {
      const a = await post(base, key, "/v1/messages", {
        model, max_tokens: 8, messages: [{ role: "user", content: "Reply with exactly: PONG" }],
      }, { "x-api-key": key, "anthropic-version": "2023-06-01" });
      const aMsg = a.j?.content?.[0] || {};
      console.log(`  anthropic /v1/messages st=${a.status} id=${idStyle(a.j?.id)} model=${a.j?.model || "-"} blocks=${(a.j?.content || []).map(b => b.type + (b.signature !== undefined ? "(sig)" : "")).join(",")} usage=${a.j?.usage ? JSON.stringify(a.j.usage) : "-"} err=${a.j?.error?.message || ""}`);
      const r2 = await post(base, key, "/v1/responses", { model, input: "Reply with exactly: PONG" });
      const outs = (r2.j?.output || []).map(o => `${o.type}:${idStyle(o.id)}`);
      console.log(`  responses /v1/responses st=${r2.status} id=${idStyle(r2.j?.id)} model=${r2.j?.model || "-"} out=[${outs.join(" ")}] usage=${r2.j?.usage ? JSON.stringify(r2.j.usage).slice(0, 180) : "-"} err=${r2.j?.error?.message || ""}`);
    }
  }
}

main().catch(e => { console.error(e.message || e); process.exit(1); });

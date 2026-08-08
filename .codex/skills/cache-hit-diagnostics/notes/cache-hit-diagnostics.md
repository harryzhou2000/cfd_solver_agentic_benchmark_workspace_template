# Cache-hit diagnostics for DeepSeek-compatible providers

How to measure prompt-cache hit rates on a provider (BLSC, official DeepSeek,
etc.) and how to verify that a real agentic session actually benefits from the
prefix cache. Written from the 2026-08 investigation of BLSC `DeepSeek-V4-Flash`
vs `api.deepseek.com`.

## Why this matters

Prompt caching is the dominant cost lever for long coding-agent sessions: a
300k-token context re-sent every turn costs ~1× input on a cache miss but only
~0.1× on a hit. If a provider's cache is not working (or its usage accounting
lies about it), a session silently burns 10× the intended input tokens.

## The two-layer diagnosis

1. **Probe layer** (`scripts/probe_*.js`): synthetic, controlled requests that
   answer "does this provider cache, and does it report it truthfully?"
2. **Session layer** (`scripts/session_cache_stats.mjs`): real `codex` sessions
   correlated with `~/.opencodex/usage.jsonl` to answer "what did the agent
   actually pay, per turn, head vs tail?"

Run the probe layer first — a provider that fails the probe will also corrupt
the session-layer numbers (see the BLSC stub-usage case below).

## Probe scripts

All three read keys from `~/.opencodex/config.json` (`providers.BLSC.apiKey` /
`apiKeyPool`, `providers.deepseek.apiKey`) with `BLSC_KEY` / `DEEPSEEK_KEY` env
fallbacks, pass keys via the environment, and never print them.

| Script | Question it answers | Shape |
| --- | --- | --- |
| `probe_model_cache.js` | Consecutive identical requests: does request 2..N show `cached ≈ input`? | `--count 5 --repeat 120 --turns 5 --effort max`, with/without `reasoning_content` history |
| `probe_real_turns.js` | Growing conversation with real model outputs: does cache grow with context? | `--steps 5 --repeat 3`, `--shuffle` for non-repeating user content |
| `probe_tool_turns.js` | Agentic two-request turns (tool call + tool result): cache across tool boundaries, streaming vs non-streaming | `--steps 5 --stream --system --repeat 3` |

### Reading the output

Healthy cache (non-streaming):

```
turn 1 B(report): in=454 cached=384 out=48   # 84.6% after first miss
repeat #1:       in=454 cached=454 out=20    # 100% on exact re-send
```

Suspicious streaming stub (BLSC before the fix):

```
turn 1 B(report): in=113 cached=0 out=1
repeat #1:       in=113 cached=0 out=1
```

`out=1` on a streaming response is the tell: the endpoint returns a stub usage
chunk (`completion_tokens: 1`) instead of real accounting. The same request
without `--stream` reports real numbers, so the cache exists — the streaming
usage chunk is stubbed. Treat any session whose streaming rows show
`out=1, cached=0` as *unmeasurable*, not *cache-missing*.

### Known probe gotchas

- **Reasoning content**: DeepSeek-compatible endpoints accept
  `reasoning_content` in assistant history only when the model is configured for
  it. ocx's `preserveReasoningContentModels` controls replay; without it the
  provider sees a shorter prefix and cache numbers look wrong.
- **Tool-call turns**: tool schema + tool-result messages sit between user text
  and the final question; a healthy prefix cache must show rising `cached` even
  across those boundaries. Zero cache on every tool turn + `out=1` = stub
  accounting, not empty cache.
- **System prompt**: a real coding agent carries a 10–20k-token system block;
  use `--system` in `probe_tool_turns.js` to approximate it, or the head of a
  real session will cache better than production.
- **Exact re-send**: `--repeat N` re-sends the identical final body; near-100%
  there is the cheapest cache-health check.

## Session-layer analysis

`scripts/session_cache_stats.mjs` correlates one codex session to its ocx usage
rows and prints head/tail/middle/all cache stats, the warm-up curve, anomalies,
and hourly buckets:

```bash
node scripts/session_cache_stats.mjs --session 019fbd32-... \
    --usage ~/.opencodex/usage.jsonl
```

Correlation mechanism: ocx persists `conversationId = sha256(session_id)[:32]`
(`src/server/request-log-conversation.ts`), where the session id is the codex
rollout's `session_meta.session_id`. The script computes the digest, filters
`usage.jsonl`, and buckets by timestamp.

What the head/tail comparison tells you:

- **Head**: cold-start warm-up. First request typically 20–30% cached (system
  prompt from a prior session), then 95–99% by request 3–10. Sustained <80%
  after request 5 means the cache is broken or the prefix keeps changing.
- **Tail**: the whole context has stabilized; expect ≥95%, ideally ≥99%.
- **Anomalies**: rows with `status != 200` or `in=0` are failures, not cache
  misses — exclude them from the rate. `cached=0` on a 200 with `out>1` is a
  genuine miss (context changed, compaction, key rotation).

## Case studies (2026-08)

### official DeepSeek (`deepseek-v4-flash`, api.deepseek.com) — healthy

`codex_dsv4_flash_03` session `019fc2bb-3f78-7202-a0de-cfa25e061493`:

| Window | n | input | cached | hit % |
| --- | --- | --- | --- | --- |
| Head (first 60) | 60 | 6.47M | 6.39M | 98.9% |
| Middle | 60 | 26.5M | 26.5M | 99.9% |
| Tail (last 60) | 60 | 8.09M | 8.07M | 99.8% |
| **All** | 1542 | 434M | 432M | **99.4%** |

Warm-up: request 1 = 21.4% (system prefix from an earlier session), request 2 =
96.7%, request ~10 = 99%.

### BLSC (`DeepSeek-V4-Flash`, llmapi.blsc.cn) — stub accounting, then fixed

`codex_dsv4_flash_02` session `019fbd32-24d8-71e3-ae8f-cdb70fb1d494`:

| Window | n | input | cached | hit % |
| --- | --- | --- | --- | --- |
| Head (first 60) | 60 | 11.8M | 0.18M | 1.5% |
| Middle | 60 | 15.7M | 0.53M | 3.4% |
| Tail (last 60) | 60 | 11.0M | 10.5M | 95.8% |
| **All** | 2757 | 744M | 27.7M | 3.7% |

Before 2026-08-02 23:41 nearly every row is `out=1, cached=0` — the streaming
usage chunk was stubbed, so the session layer **could not measure** the cache
(which was actually working: the occasional non-streaming request showed
97–98.8%). After the upstream provider fixed its streaming accounting, rows
report real usage and the tail sits at 99.4–99.9% hit.

Lesson: always probe before trusting session-layer numbers, and report the
`out=1` stub signature to the provider with a before/after `probe_tool_turns.js
--stream` capture.

## Related

- `notes/codex-session-history.md` — where codex/ocx keep session state, and how
  to extract per-session/per-turn history.
- `ocx-relay/` — wire-capture relay used to verify reasoning-content
  preservation on the ocx→provider hop.

## Case study: deepseek-v4-pro probes + opencode late-summary cache loss (2026-08-03)

Symptom: an opencode orchestrator session (`ses_040b656bdffeVJcS8XFIqGFGTw`,
omo_slim_dsv4_01, opencode 1.18.11, deepwork plugin) showed only 10.8% prompt
cache hits (cached 1.65M of 15.18M prompt, 120 requests), while its
fixer/oracle subagent children cached at 99.2%. Was the provider broken?

Probes against `api.deepseek.com` `deepseek-v4-pro` (effort max):

- `probe_model_cache.js --targets deepseek-pro`: #1 miss, #2-5 cached 1280/1306
  (~98%) — identical-request caching works.
- `probe_tool_turns.js --targets deepseek-pro` (non-stream): growth 68-94%,
  re-send 97.8% — healthy.
- same with `--stream`: growth 68-95%, re-send 94.3%, no stub rows
  (no `cached=0, out=1`) — streaming accounting is real.
- `probe_real_turns.js --targets deepseek-pro`: growing conversation with
  reasoning_content replay 48-83%, re-send 99.5% — reasoning replay does not
  break caching.

Conclusion: the provider is healthy; the client was breaking its own cache.

Session forensics (opencode SQLite store):

- Requests 1-15 warm up normally (cached 0 → 56,704).
- At 21:47:02 UTC the **first user message was mutated in place**: opencode
  attached `message.data.summary.diffs` (.gitignore/.ignore edits) after the
  message had already been sent and cached.
- The next request (21:50:39 UTC) collapsed from cached=56,704 to cached=896,
  then stayed pinned at ~9,216 for 100+ requests despite tiny per-request
  additions (200-3K tokens). The pinned value ≈ system prompt + first user
  message.
- All 18 orchestrator user messages carry `summary.diffs`; 14 were attached
  late (after the message was sent); the last two arrived 1-2 hours late with
  124 and 117 file diffs.
- Children had only 4 user messages → rare mutations → 99.2%. The single
  child zero-cache request (in=201,533 at 11:24 local) coincides with a
  compaction event (`agent=compaction` stream in opencode.log).

Mechanism: DeepSeek prefix caching is byte-exact. opencode serializes the
per-message `summary.diffs` into the request body, so attaching/updating a
summary on an already-sent message invalidates the provider cache from that
message onward, every time it happens.

Related opencode issues/PRs:

- #21518 — queued user messages wrapped in `<system-reminder>` serialize
  inconsistently across turns, breaking prompt caching (closed "not planned",
  2026-04-08). Same failure family (stored-vs-sent serialization drift).
- #24104 / #24190 / #24130 + PRs #24146 / #24200 / #24435 — DeepSeek
  `reasoning_content` round-trip (fixed April 2026; present in 1.18.11; NOT
  the cause here — children replay reasoning and still cache at 99%).
- #4416 — auto-compaction earlier than expected with caching enabled
  (provider/version-specific; explains occasional one-off big misses).
- #4317 — feature request for fork-aware cache keys (`prompt_cache_key`).

Takeaways for the skill: when a session shows a persistent low plateau
(cached ≈ system prompt + first message), suspect client-side mutation of
already-sent messages (summaries, queued-message wrappers, in-place part
rewrites by plugins), not the provider — probe the provider first, then check
the session store for late `time_updated` on user messages.

## UPDATE (2026-08-03, later same day): root cause found — the goal plugin

Wire capture + forensics overturned the "late summary" mechanism above. The
pinned sessions were broken by **`@prevalentware/opencode-goal-plugin`**
(loaded globally from `~/.opencode/opencode.json`), not by summary diffs.

### Mechanism

The plugin's `experimental.chat.system.transform` hook calls
`mergeSystemReminder()`, which appends a "goal mode active reminder" block to
the **end of the system prompt on every request** while the session has an
active goal. The reminder contains per-request volatile counters:

```text
Budget:
- Time spent pursuing goal: 66 seconds      <- wall clock since goal creation
- Tokens used: 8718                          <- plugin's cumulative accounting,
                                                updated on every request
- Token budget: none
- Tokens remaining: unbounded
- Auto-continues used: 0/25                  <- increments on auto-continue
- Duration limit: none
```

DeepSeek prefix caching is byte-exact, so the cache breaks at the **first
changed number — a fixed byte position** (the budget line). Everything after
it (rest of system + entire message history + all tool definitions) is re-sent
as new tokens every request. Hence the observed signature: `cache.read` pins
at a **constant** value ≈ system-prompt tokens before the reminder, while
`in_new` grows by the whole history each turn.

### Evidence

- Both bad sessions had **active goals** in
  `~/.local/share/opencode-goal-plugin/goals.json`, created at session start:
  kimi `ses_04148d309ffegp6AE6TufXkdJW` (10.3%, goal created 19:05:09,
  `timeUsedSeconds` 147,416 / `tokensUsed` 22.3M at close), dsv4
  `ses_040b656bdffeVJcS8XFIqGFGTw` (10.8%, goal created 21:50:39), plus a
  third goal session `ses_03e6cdbcaffefVyMLw9N6OIQGR` (6.8%).
- **Every goal-less session** on the same opencode 1.18.11, same workspaces,
  same `OPENCODE_EXPERIMENTAL_BACKGROUND_SUBAGENTS=true`, and omo-slim loaded
  cached at 95–99.6% (including a 378-request session at 99.6%). The
  background-subagent flag is **exonerated**: wire capture with the flag on
  and no goal was perfectly cache-stable (system + tools byte-identical,
  messages append-only, 8,704/8,705 cached).
- The July "1.18.3 regression" (36.4%) is the same plugin: the only large
  1.18.3 session (1,006 reqs, 13.3%) had a goal created 2026-07-18 06:30Z —
  matching the `~/.opencode/opencode.json` mtime (14:28 +08:00) when the
  plugin was added. A concurrent goal-less 1.18.3 session cached 93.6%, and
  1.18.2 (July 16, 86.2%) predates the install. Subagent children have no
  goal entry → no reminder → 96–99% (matches the fixer/oracle numbers).
- Live repro through `scripts/opencode_wire_proxy.mjs` (1.18.11 +
  deepseek-v4-pro): with a seeded active goal, consecutive requests' system
  prompts differed at the same fixed byte — `Time spent pursuing goal: 66 →
  74 → 82 seconds`, `Tokens used: 8718 → 18035 → 27373` — and the response
  usage collapsed from cached 8,704/8,705 (99.9%) to 1,920/9,296 (20.6%) on
  the first goal request, staying pinned.

### Why summaries were a red herring

omo-slim attaches `summary.diffs` to user messages, and the goal sessions are
exactly the ones that accumulate them; but (a) the kimi session had omo-slim
disabled and still pinned, (b) the legacy serializer
(`MessageV2.toModelMessagesEffect`) does not read `message.summary` at all,
and (c) the wire diff places the break in the **system prompt**, not in any
message.

### Diagnosis recipe

1. Run the opencode scanner and look for the **constant-pin signature**:
   `cache.read` flat (e.g. 18,176) for 100+ requests while `in_new` grows.
2. Check for an active goal:
   `~/.local/share/opencode-goal-plugin/goals.json` (or
   `OPENCODE_GOAL_STATE_PATH`) — session ID present with `status != complete`.
3. Optionally repro on the wire (see `scripts/opencode_wire_proxy.mjs`):
   seed a goal entry and diff consecutive system prompts.

### Fix options (upstream)

- Do not render per-request volatile counters into the system prompt. Keep
  the reminder static and refresh it only on goal status changes.
- Or append the reminder **after the message history** (or into the last user
  message), so the byte-exact prefix up to the reminder stays cached and only
  the small tail is re-sent.
- Same guidance for any plugin hook that renders mutable state into the
  system prompt: omo-slim's background-job board and running-task placeholder
  rewrites are the same failure family when active.

## BLSC backend identity (probed 2026-08-07)

BLSC (`https://llmapi.blsc.cn/v1`, openai-compatible) is a **LiteLLM proxy**
in front of **vLLM**:

- Invalid key error literally names LiteLLM's token table:
  "Unable to find token in cache or `LiteLLM_VerificationTokenTable`" with
  `"type":"token_not_found_in_db"` (LiteLLM's virtual-key error type).
- Malformed/missing key: "Authentication Error, Malformed API Key passed in.
  Ensure Key has `Bearer ` prefix." (LiteLLM proxy auth text).
- Model access denied: `"type":"team_model_access_denied"` with a
  team-scoped model allow-list (LiteLLM proxy team model access). Error
  envelope is LiteLLM's `{"error":{message,type,param,code}}`.
- Successful completion carries `"system_fingerprint":"vllm-0.21.0-dp16-ep-..."`
  and `provider_specific_fields` (routed_experts/stop_reason/token_ids) —
  upstream is vLLM 0.21.0 (dp16 = 16-way data parallel, ep = expert parallel).
- HTTP layer: `server: istio-envoy` on the 401 path (Istio mesh in front).

Implication for cache probes: LiteLLM relays upstream usage accounting; the
cached-token numbers come from the vLLM/DeepSeek upstream, not the proxy.

## BLSC Kimi-K3: no prompt caching (probed 2026-08-07)

Identical-request probes against `llmapi.blsc.cn/v1` show **Kimi-K3 caches at
0%** while DeepSeek models on the same proxy cache at ~98%:

- `probe_model_cache.js --targets blsc-k3 --count 8` (1577-token prompt,
  reasoning_effort=max): cached=0 on all 8 requests; input stays 1577 every
  request (no prefix reuse); outputs real (88-256), so not stub accounting.
- Longer prompt (6130 tokens, 3 identical): cached=0 on all 3; latency stays
  ~8-13s/request.
- Streaming (stream_options include_usage): usage is reported truthfully
  (prompt_tokens 1325, completion 32) with `prompt_tokens_details.cached_tokens`
  = 0 — genuine miss, not a `cached=0, out=1` stub.
- Control `DeepSeek-V4-Flash` (6011-token prompt, same proxy/request shape):
  req1 miss, req2+ cached=5888 (98.0%), latency 13.5s -> 0.8s.

Conclusion: BLSC's Kimi-K3 route does not expose/leverage upstream context
caching. Sessions on Kimi-K3 via BLSC pay full input every request; prefer
DeepSeek models on BLSC or a Kimi route that caches (e.g. the oc_goal-kimik3
session that showed 98.5% used a different provider route).

### Kimi-K3 re-probe 2026-08-08: caching now partially engages

Same endpoint, `probe_model_cache.js --targets blsc-k3 --count 6 --effort low`
(identical requests, reasoning shape):

```
#1-#4  cached=0 (miss)
#5     cached=1280 (95% of the 1342-token prompt)
#6     cached=1280
=> 2/6 cached>0  overall 31.8%
```

So the route now warms up, but only after several misses — consistent with
multiple backend replicas each warming its own cache (requests hit a warmed
replica only after enough samples). Expect low head rates that improve as a
session reuses the same replica. Endpoint latency is highly variable today
(single `max_tokens=8` request: 5s; an `effort=max` probe did not complete
request #1 within 300s), so budget probe timeouts generously.

### Kimi-K3 streamed probe 2026-08-08 (tool-turn shape, ~15k system block)

`probe_tool_turns.js --targets blsc-k3 --stream --system --steps 3 --repeat 3
--effort low`:

```
turn 1 A: in=11006 cached=8832 (80.2%)   turn 1 B: in=11107 cached=8832 (79.5%)
turn 2 A: in=11230 cached=8832 (78.6%)   turn 2 B: in=11326 cached=8832 (78.0%)
turn 3 A: in=11414 cached=8832 (77.4%)   turn 3 B: in=11504 cached=8832 (76.8%)
repeat x3 (exact re-send): in=11504 cached=8832 (76.8%) every time
```

- Streaming usage is truthful for Kimi-K3 (real cached + real output tokens;
  NOT the `cached=0, out=1` stub signature).
- The cache covers a **fixed ~8.8k-token prefix** (the stable system block)
  and nothing else: `cached` is constant while `in` grows, and even exact
  re-sends of the final request do not cache the conversation/tool tail.
- Net effect: K3 sessions on BLSC see ~80% early, decaying as context grows;
  only the system prefix is ever cached. DeepSeek models on the same proxy
  cache the full prefix (~98%) — prefer them for long agentic sessions.

## BLSC backend fingerprints: LiteLLM model groups (probed 2026-08-08)

BLSC is a **LiteLLM router**: each model name is a model *group* with one or
more upstream entries. The per-request `x-litellm-model-id` header identifies
which entry served a call, and the router can switch entries between requests
(load balancing / fallbacks). **Probe repeatedly and key observations by
model-id** — a single capture can hit a different upstream.
`scripts/probe_backend_fingerprint.js --samples N --tool --surfaces` automates
the sampling.

Observed entries (all on `llmapi.blsc.cn`, 2026-08-08):

`Kimi-K3` — entry `d285d5d7` (stable across 7/7 requests):
- id `chatcmpl-<ULID26>` (Crockford base32; first 10 chars decode to the
  request timestamp), `llm_provider-x-request-id` = same ULID,
  `llm_provider-x-trace-id` = same value undashed.
- No `system_fingerprint`, no `llm_provider-server` header.
- usage: `prompt_tokens_details.{audio_tokens, cached_tokens}` +
  `completion_tokens_details.{audio_tokens, reasoning_tokens}`.
- message `provider_specific_fields`: `{refusal}` on plain answers,
  `{reasoning, refusal, reasoning_content}` on tool calls.
- tool-call ids: `chatcmpl-tool-<16hex>`.
- Upstream 400 for bad effort: `invalid request: Kimi K3 reasoning_effort must
  be one of high, low, max; got '...' (request id: <ULID>)` with `type: null`.

`DeepSeek-V4-Flash` — group rotates between ≥3 entries:
- `cc7b25a9` (current; 6/6 in one window): id bare uuid4 (= x-request-id),
  no fingerprint, usage `prompt_tokens_details.{cached_tokens}` +
  `completion_tokens_details.{reasoning_tokens}`, message
  `provider_specific_fields: {refusal, reasoning_content}`, tool ids
  `call_00_<22 chars>`.
- `81a86146` (1 hit): vLLM 0.21.0 — id `chatcmpl-<uuid4-dashed>`,
  `system_fingerprint: vllm-0.21.0-dp16-ep-...`, `llm_provider-server: uvicorn`,
  message psf `{reasoning, refusal}`, choice psf
  `{routed_experts, stop_reason, token_ids}`.
- `f54a497e` (error path): pydantic-style `'reasoning_effort' must be one of:
  'low', 'medium', 'high', 'xhigh', 'max'`, `type: invalid_request_error`.

API surfaces: Anthropic-compatible `/v1/messages` and OpenAI Responses
`/v1/responses` are both exposed for these models, and the per-entry id
fingerprint passes through all of them (K3 message ids stay
`chatcmpl-<ULID>`, DS ids stay bare uuid4 — including the Responses
`output[].message.id`). DS Anthropic responses contain a `thinking` block with
`signature: null` (LiteLLM conversion).

### vs official docs

Kimi official (`api.moonshot.cn/v1`; docs source `lyhmoonshot/mintlify_ai`,
rendered at platform.kimi.com/docs):
- Docs examples use `id: cmpl-<32hex>` (2023-era example, `created`
  1698999496) and flat `usage.cached_tokens` — no details objects, no
  `audio_tokens`, no `provider_specific_fields` documented.
- K3: `reasoning_effort` low/high/max (default max), always-reasoning,
  `max_completion_tokens` default 131072 / max 1048576, `response_format` +
  `strict`.
- Errors: OpenAI-style `{"error":{message,type,param,code}}` with
  `invalid_request_error`; message family `Invalid request: ...` and
  `invalid <param>: only <value> is allowed for this model`; `request_id`
  referenced when contacting support.

DeepSeek official (`api-docs.deepseek.com/api/create-chat-completion`):
- Non-stream example id `930c60df-...` (bare uuid4); stream example id 32-hex;
  `system_fingerprint: fp_...`.
- Usage: flat `prompt_cache_hit_tokens` / `prompt_cache_miss_tokens` plus
  `completion_tokens_details.{reasoning_tokens}`.

Reading the observed envelopes against those: the DS `cc7b25a9` entry shares
the official DeepSeek bare-uuid id style and reasoning usage, but reports
`prompt_tokens_details.cached_tokens` instead of flat
`prompt_cache_hit_tokens` and no fingerprint (LiteLLM deepseek usage mapping).
The K3 entry matches **no** documented envelope exactly (ULID ids,
`audio_tokens` usage details, `chatcmpl-tool-` ids, custom validation text) —
it is a Kimi-compatible gateway distinct from both the documented Moonshot API
and the vLLM/DeepSeek stack; treat the docs' `cmpl-<32hex>` example as stale.

Caching consequence: the fixed ~8.8k-token cacheable prefix observed on
Kimi-K3 belongs to entry `d285d5d7`; the DS group's rotating entries each
carry their own cache state, so sessions that switch entries pay re-warmup.

### Responses API notes (probed 2026-08-08, DeepSeek-V4-Flash)

`/v1/responses` works for DS-V4-Flash (non-stream, `stream: true`, and tools),
but treat it as a conversion layer with caveats:

- **Non-streaming cache accounting is truthful and cross-surface.** Paired
  test with a ~5400-token identical prompt on entry `cc7b25a9`: chat
  completions warmed on request 2 (`cached=5376`), and the *first* responses
  request with the same string input already reported `cached=5376` —
  LiteLLM's responses->chat conversion produces byte-identical upstream
  prompts and propagates the cached count. (An earlier tiny-prompt probe
  reported `cached=0` on both surfaces — 89-token prompts sit below the
  backend's cacheable prefix, and chat showed the same zeros; the claim that
  responses "never caches" was wrong.)
- **Streaming responses usage is bare** (`{input_tokens, output_tokens,
  total_tokens}`, no details), so measure cache via non-streaming responses
  (or chat completions).
- The route rotates the same LiteLLM entries as chat completions (observed
  `81a86146`, `cc7b25a9`, plus a fourth entry `5da5edaa`), so per-request
  envelopes vary.
- Tool calls come back as a `function_call` output item with
  `call_id: chatcmpl-tool-<16hex>` regardless of entry — the conversion layer
  normalizes ids (chat completions show per-entry formats like `call_00_...`).
- Outer response id is `resp_<449 chars>` (a long encoded blob, not OpenAI's
  short `resp_` ids); reasoning arrives as a `reasoning` output item and, in
  streaming, as `response.reasoning_summary_text.delta` events.
- Envelope defaults differ: `temperature: 0.0`, `parallel_tool_calls: false`,
  `max_output_tokens: null` in the returned object.

### Responses API tool-call shape (`probe_tool_turns.js --surface responses`)

The tool-turn probe now supports `/v1/responses` (chat-style messages are
converted to `input` items; Responses-style tools need `name`/`parameters` at
top level, not a chat-style `function` wrapper). DS-V4-Flash with the ~15k
system block:

- Non-stream: turn 1 A `cached=11008/11095 (99.2%)` — identical to the chat
  completions run on the same entry (same upstream prompt bytes); later turns
  92-99%; exact repeats 91.4%.
- Streamed: tool calls work and content parses, but usage is bare (no
  `cached_tokens` details) — for cache numbers use non-streaming.

### Tiny prompts don't cache on official DeepSeek either (probed 2026-08-08)

The 89-token `Reply with exactly: PONG` body that reports `cached=0` on BLSC
was tested verbatim against the official DeepSeek API (`api.deepseek.com`,
`deepseek-v4-flash`): all 5 identical requests reported `cached_tokens=0`.
Same for the first request of a 5400-token prompt; requests #2/#3 cached
**5376 of 5400 tokens — exactly the numbers BLSC entry `cc7b25a9` reports**
(5400 in, 5376 cached, 24 miss). So the tiny zero-hit behavior is a
DeepSeek-side minimum-prefix policy, not a BLSC artifact — and `cc7b25a9`
fingerprints as the official DeepSeek API upstream (identical tokenizer
counts and cache accounting). Official usage carries both
`prompt_tokens_details.cached_tokens` and flat `prompt_cache_hit_tokens` /
`prompt_cache_miss_tokens`; BLSC's LiteLLM layer surfaces only the details.

### Session stickiness (LiteLLM router affinity)

LiteLLM has `DeploymentAffinityCheck` (`litellm/router_utils/pre_call_checks/
deployment_affinity_check.py`): a **Router-only** pre-call check, enabled in
code via `Router(optional_pre_call_checks=["session_affinity", ...])` — not a
proxy `config.yaml` knob (no proxy wiring found). It keeps a TTL-bounded cache
mapping `(model group, session id | API-key hash) -> deployment model id`;
the read path (`async_filter_deployments`) applies to **chat completions too**,
the write path runs pre-call, and the request side passes
`litellm_metadata={"session_id": ...}`. Flags: `session_affinity`,
`deployment_affinity` (keyed on the proxy-side `user_api_key_hash`, not the
OpenAI `user` param), `responses_api_deployment_check` (`previous_response_id`
continuity, Responses API only), `encrypted_content_affinity`; per-group
override via `model_group_affinity_config`.

Probed on BLSC (2026-08-08): sending `metadata.session_id` on DS-V4-Flash
chat completions does **not** pin — the same session id across 6 requests
still rotated (`81a86146` once, then `cc7b25a9`), identical to the no-session
control. BLSC has not enabled the affinity check (or does not forward client
metadata into routing kwargs). Kimi-K3's group looks naturally pinned because
it has a single upstream entry (`d285d5d7`).

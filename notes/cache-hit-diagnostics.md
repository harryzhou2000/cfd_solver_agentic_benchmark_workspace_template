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

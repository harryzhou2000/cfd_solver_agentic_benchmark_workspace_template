---
name: cache-hit-diagnostics
description: Diagnose LLM prompt-cache hit rates and token usage for DeepSeek-compatible providers (BLSC, api.deepseek.com) and for codex/opencodex sessions. Use when asked to check whether a provider's prompt cache is working, measure cached vs input tokens on real agent sessions, explain suspicious cache-hit numbers (e.g. cached=0 with out=1), compare providers (BLSC vs official DeepSeek), extract per-session token/cache history from ~/.opencodex/usage.jsonl or ~/.codex/sessions rollouts, or reproduce agentic request shapes (reasoning_content, tool calls, streaming) against a provider API.
---

# Cache Hit Diagnostics

Measure and explain prompt-cache hit rates at two layers: synthetic probes
against the provider API, and real codex/opencodex session accounting.

## When to use

- "Is BLSC/DeepSeek caching working?"
- "Why does this session show 0 cached tokens?"
- "Compare cache hit rates between providers"
- "Extract token/cache history for codex session X"
- "What did the agent pay in cached vs input tokens, head vs tail?"

## Quick start

Run the repo scripts from the workspace root (they read keys from
`~/.opencodex/config.json` or `BLSC_KEY`/`DEEPSEEK_KEY` env; keys are never
printed):

```bash
# 1. Probe: does the provider cache, and does it report truthfully?
node scripts/probe_model_cache.js --targets blsc-flash,deepseek-flash
node scripts/probe_tool_turns.js --steps 5 --stream --system --repeat 3

# 2. Session: what did a real codex session pay?
node scripts/session_cache_stats.mjs --cwd codex_dsv4_flash_03
node scripts/session_cache_stats.mjs --session <session-id>
```

## Workflow

### 1. Probe the provider first

Synthetic probes tell you whether the provider's cache works and whether its
usage accounting can be trusted. Never interpret session numbers before this
step — a provider with stub accounting makes every session look like a cache
miss.

- `scripts/probe_model_cache.js` — N identical requests; expect request 1 miss,
  then `cached ≈ input`.
- `scripts/probe_real_turns.js` — growing conversation reusing real assistant
  output (including `reasoning_content`); expect rising `cached`.
- `scripts/probe_tool_turns.js` — agentic tool-call turns; compare `--stream`
  vs non-streaming. **`cached=0, out=1` on streaming = stub usage chunk**, not
  an empty cache.

Flags that matter: `--effort max`, `--turns N`, `--reasoning-repeat N`,
`--system` (approx. real agent system prompt), `--repeat N` (exact re-send).

### 2. Correlate a session with ocx usage

ocx stores `conversationId = sha256(session_id)[:32]` in
`~/.opencodex/usage.jsonl`. The helper automates the join:

```bash
node scripts/session_cache_stats.mjs --cwd <workspace-substring> [--head 60] [--tail 60]
node scripts/session_cache_stats.mjs --session <id> [--usage ~/.opencodex/usage.jsonl] [--json]
```

Output: head/middle/tail/all cache rates, warm-up curve, anomalies (non-200,
real-200-with-zero-cache), hourly buckets, and stub-usage counts.

Read the results as:
- HEAD: warm-up; first request ~20–30% cached, ≥95% by request ~5–10.
- TAIL: steady state; expect ≥95%, ideally ≥99%.
- `out=1, cached=0` rows are unmeasurable (stub accounting), not misses.
- Non-200 rows have `in=0`; exclude them from the rate.

### 3. Explain and report

- Healthy: `cached/input ≥ 95%` at tail; warm-up hits 99% quickly.
- Stub accounting signature: `cached=0, out=1` on streaming rows while
  non-streaming shows real numbers. Report to the provider with a
  `probe_tool_turns.js --stream` capture.
- Genuine misses: 200 rows with `cached=0, out>1` — context changed,
  compaction, key rotation, or reasoning-content replay not preserved.

## References

- `notes/cache-hit-diagnostics.md` — full methodology, probe gotchas, and the
  BLSC vs DeepSeek case studies (stub accounting until 2026-08-02 23:41, then
  ~99.9%).
- `notes/codex-session-history.md` — where codex/ocx keep state
  (`~/.codex/sessions/**/rollout-*.jsonl`, `~/.opencodex/usage.jsonl`,
  `ocx observe usage/logs`, `ocx debug usage`, `ocx-relay/` wire capture) and
  the correlation recipe.

## Scripts

- `scripts/probe_model_cache.js` — identical-request cache probe.
- `scripts/probe_real_turns.js` — growing-conversation cache probe.
- `scripts/probe_tool_turns.js` — tool-turn + streaming probe (stub detection).
- `scripts/session_cache_stats.mjs` — session↔usage correlation and head/tail
  cache statistics.

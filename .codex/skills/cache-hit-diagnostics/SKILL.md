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

This skill is self-contained: all scripts and notes live **inside this skill
directory** (`.codex/skills/cache-hit-diagnostics/scripts/` and
`.../notes/`), and every path in this file is relative to the skill directory.
Run them from the skill directory (they read keys from
`~/.opencodex/config.json` or `BLSC_KEY`/`DEEPSEEK_KEY` env; keys are never
printed):

```bash
# cd .codex/skills/cache-hit-diagnostics

# 1. Probe: does the provider cache, and does it report truthfully?
node scripts/probe_model_cache.js --targets blsc-flash,deepseek-flash
node scripts/probe_tool_turns.js --steps 5 --stream --system --repeat 3

# 2. Session: what did a real codex session pay?
node scripts/session_cache_stats.mjs --cwd codex_dsv4_flash_03
node scripts/session_cache_stats.mjs --session <session-id>

# 2b. opencode session: what did an opencode run pay (incl. subagents)?
node scripts/opencode_session_cache_stats.mjs --session <ses_id> [--subagents]
node scripts/opencode_session_cache_stats.mjs --workspace <dir-substring> [--subagents]

# 3. opencode wire capture: which bytes break the cache between requests?
node scripts/opencode_wire_proxy.mjs 8791 /tmp/oc-wire api.deepseek.com /v1
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

### 2b. opencode sessions (SQLite store)

opencode keeps sessions in `~/.local/share/opencode/opencode.db`; per-request
usage lives on each assistant `message.data.tokens` where `input` is the
**non-cached** prompt portion and `cache.read` the cached prefix, so
hit rate = `cache.read / (input + cache.read + cache.write)`. Subagent
sessions link through `session.parent_id` and carry an `agent` label
(e.g. orchestrator/fixer/oracle). `scripts/opencode_session_cache_stats.mjs`
scans the DB directly (no server needed):

```bash
node scripts/opencode_session_cache_stats.mjs --session <ses_id> --subagents
```

`--subagents` adds the per-child breakdown plus a combined window; `--json`
emits the machine-readable report. Full schema and caveats in
`notes/opencode-session-history.md`.

### 3. opencode: check for per-request system-prompt mutations

The #1 opencode-side cache killer found so far is a plugin that renders
**mutable state into the system prompt**. Known case:
`@prevalentware/opencode-goal-plugin` appends a "goal mode active reminder"
with volatile counters (`Time spent pursuing goal: N seconds`, `Tokens used:
N`, `Auto-continues used: N`) to the system prompt on every request while a
goal is active. DeepSeek's byte-exact prefix cache then breaks at the same
fixed position every request → `cache.read` pins at a **constant** value
(≈ system-prompt-before-reminder) while `in_new` grows with the full history.

Diagnose:

```bash
# 1) constant-pin signature: flat cache.read + growing in_new
node scripts/opencode_session_cache_stats.mjs --session ses_<id>

# 2) active goal for that session?
node -e "const g=require(process.env.HOME+'/.local/share/opencode-goal-plugin/goals.json').goals; console.log(g['ses_<id>']?.status)"

# 3) wire proof: proxy opencode and byte-diff consecutive system prompts
node scripts/opencode_wire_proxy.mjs 8791 /tmp/oc-wire api.deepseek.com /v1
```

`OPENCODE_EXPERIMENTAL_BACKGROUND_SUBAGENTS` and omo-slim's summary-diff
attachments were investigated and **exonerated** for the pinned sessions;
the goal-plugin reminder is the cause (see
`notes/cache-hit-diagnostics.md` "UPDATE" section and
`notes/opencode-session-history.md`). Subagent children have no goal entry,
which is why they cache at 96–99% while the orchestrator pins.

Environment gotcha: unset `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY` when running
the wire proxy — a LAN proxy intercepts `127.0.0.1` and returns 503 with
nothing logged.
### 4. Explain and report

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
- `notes/opencode-session-history.md` — opencode's SQLite session store,
  message-level token/cache schema, subagent `parent_id` extraction, and the
  direct-DB scanning recipe.
- `notes/goal-plugin-no-interrupt.md` — goal-plugin fork/PR (#53) adding the
  `noInterruptOnUserMessage` option, the three gated pause sites, and how
  synthetic background-task messages falsely paused goals.

## Scripts

- `scripts/probe_model_cache.js` — identical-request cache probe.
- `scripts/probe_real_turns.js` — growing-conversation cache probe.
- `scripts/probe_tool_turns.js` — tool-turn + streaming probe (stub detection).
- `scripts/session_cache_stats.mjs` — session↔usage correlation and head/tail
  cache statistics.
- `scripts/opencode_session_cache_stats.mjs` — opencode session cache stats
  from the SQLite store, with recursive subagent extraction (`--subagents`).
- `scripts/opencode_wire_proxy.mjs` — logging reverse proxy to byte-diff
  consecutive opencode provider requests (finds which bytes break caching).

Probe targets: `blsc-flash`, `deepseek-flash`, `deepseek-pro` (official
`api.deepseek.com`, `deepseek-v4-pro`).

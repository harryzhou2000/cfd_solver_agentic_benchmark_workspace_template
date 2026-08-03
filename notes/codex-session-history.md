# Extracting codex/opencodex session history for token & cache analysis

Where the data lives, what each file contains, and how to correlate a codex
session with the requests ocx actually sent upstream.

## Data locations

| Data | Path | Format |
| --- | --- | --- |
| Codex session rollouts | `~/.codex/sessions/YYYY/MM/DD/rollout-<ts>-<session_id>.jsonl` | JSONL, one record per line |
| Session index | `~/.codex/session_index.jsonl` | `{id, thread_name, updated_at}` |
| Input history | `~/.codex/history.jsonl` | prompt/command history per session |
| ocx usage log | `~/.opencodex/usage.jsonl` (or `$OPENCODEX_HOME/usage.jsonl`) | per-request usage, incl. cached tokens |
| ocx usage debug | `~/.opencodex/usage-debug.jsonl` | raw upstream body samples + extracted usage (`ocx debug usage on`) |
| ocx request logs | `ocx observe logs` / `ocx observe usage` | runtime API over the management port |
| Wire capture | `ocx-relay/` (`/tmp/ocx-relay.log`, `...-resp.log`) | raw request/response bodies on the ocx→provider hop |

## Codex rollout records

Each rollout is a JSONL of typed records:

- `session_meta` — session id, cwd, model provider, CLI version, start time.
  **Find sessions for a workspace** with:
  `rg -l "<workspace-name>" ~/.codex/sessions/YYYY/MM/DD/*.jsonl` and then read
  the `session_meta` cwd.
- `turn_context` — per-turn metadata: turn id, model, effort, cwd, sandbox
  policy, summary.
- `response_item` — messages (user/assistant/developer), tool calls/results,
  reasoning items. Reasoning payloads are **encrypted** at rest
  (`encrypted_content`); logs can prove reasoning happened, never what it said.
- `event_msg` / `compacted` / `world_state` — events, compaction boundaries,
  workspace state.

Rollouts contain conversation content but **not** token/cache accounting; the
numbers live in ocx's `usage.jsonl`.

## Correlating a session with ocx usage

ocx persists `conversationId` as `sha256(session_id)[:32]`
(`opencodex/src/server/request-log-conversation.ts`). So:

```python
import hashlib
conversation_id = hashlib.sha256(session_id.encode()).hexdigest()[:32]
```

Then filter `~/.opencodex/usage.jsonl` rows by that `conversationId`. Each row
has: `requestId`, `timestamp`, `provider`, `model`, `requestedModel`,
`requestedEffort`/`effectiveEffort`, `status`, `durationMs`, `firstOutputMs`,
and `usage{inputTokens, outputTokens, cachedInputTokens,
reasoningOutputTokens}`.

`scripts/session_cache_stats.mjs` automates this: it takes a session id (or
workspace path to discover it), computes the digest, and prints head/tail
statistics plus a warm-up curve and anomalies.

## Reading the usage rows

- `cachedInputTokens` is the provider-reported prefix-cache hit (DeepSeek:
  `prompt_cache_hit_tokens`; mapped by ocx from `prompt_tokens_details` or
  `prompt_cache_hit_tokens`).
- **Watch for stub accounting**: `outputTokens=1` with `cachedInputTokens=0`
  on streaming rows means the upstream returned a placeholder usage chunk; the
  row is unmeasurable, not a cache miss (see
  `notes/cache-hit-diagnostics.md`, BLSC case).
- `status != 200` rows (`in=0`) are failures (client cancel 499, upstream
  5xx, 429 rate limits) — exclude from cache-rate math.
- `firstOutputMs ≈ durationMs` suggests a buffered response; a large gap
  suggests streaming.

## ocx runtime views

```bash
ocx observe usage --range 7d --json     # aggregate by surface/range
ocx observe logs --limit 200 --jsonl    # recent request log
ocx debug usage on                      # raw body samples → usage-debug.jsonl
ocx debug usage logs -f                 # tail the capture
```

## Session-wise token analysis recipe

1. Find the rollout(s) for the workspace:
   `rg -l "<workspace>" ~/.codex/sessions/$(date +%Y/%m)/*/*.jsonl`.
2. Read `session_meta` for the session id and start time.
3. Correlate usage rows via the sha256 digest (script above).
4. Bucket by hour/turn: input vs cached vs output; check head warm-up and tail
   steady-state.
5. Cross-check suspicious numbers against the probe scripts and, if reasoning
   content is in question, the relay capture.

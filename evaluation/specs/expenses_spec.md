# Expenses Specification (time, tokens, cost)

## Scope

Extract execution expenses of a codex contestant run: **time**, **token
usage** (main agent and all subagents), and an **estimated cost** computed
from tokens against the centralized cost metadata. Both Codex and OpenCode
selected session trees are supported.

## 1. Time

Two values are reported and never mixed:

- `goal_time_seconds`: codex-reported goal time, read directly from
  `thread_goals.time_used_seconds` for each root thread of the contestant's
  session tree (`goals_1.sqlite`). Codex tracks this field natively per goal
  thread; no estimation is involved.
- `wall_time_seconds`: computed wall-clock span of the execution — from the
  earliest to the latest event timestamp across all session rollouts in the
  contestant's thread tree (fallback: `threads.created_at`/`updated_at` when a
  rollout file is missing).

Both are summed across root threads when a workspace contains several
independent runs (multiple roots).

**Botched/abandoned sessions are included by default**: every codex session
whose `cwd` is inside the contestant workspace is part of the accounting
(roots that are `paused`/`blocked`/`active` as well as `complete`), because a
directory can accumulate failed first attempts. To keep them separable:

- `expenses.json` reports `time_seconds.by_root_tree`: per root session, its
  status, goal time, thread count, and token total.
- `--roots <thread-id,...>` restricts the extraction to the given root
  session(s) and their subagent trees (e.g. `--roots
  019fb9e3-ba6e-7e40-99d3-84d723942dc8` for the complete run only).
- `summarize.py` forwards the same `--roots` flag to all extractors.

## 2. Token usage

Authoritative source, in priority order:

1. **Terminal rollout token event** (`event_msg` with `type: token_count`):
   the final cumulative `total_token_usage` for each selected thread supplies
   exact `input_tokens`, `cached_input_tokens`, `non_cached_input_tokens`,
   `output_tokens`, `reasoning_output_tokens`, and `total_tokens`. Count every
   selected root and subagent thread once.
2. **Per-turn usage logs** (`logs_2.sqlite`, `feedback_log_body` containing
   `codex.turn.token_usage.*`): exact `input_tokens`, `cached_input_tokens`,
   `non_cached_input_tokens`, `output_tokens`, `reasoning_output_tokens`, and
   `total_tokens` per thread+model, keyed by `turn.id`. Records are grouped by
   `(thread_id, turn.id)` and summed (each logged submission consumed tokens).
3. **Thread totals** (`state_5.sqlite` `threads.tokens_used`): used for
   threads with neither rollout counters nor usage-log records; reported with
   `"source": "threads_fallback"` and no input/output split.

Token accounting:

- Every thread whose `cwd` is inside the contestant workspace is included;
  subagent threads are linked through `thread_spawn_edges` and reported
  separately (`is_subagent`) and in the aggregate.
- Totals are reported per model, per thread, and as main-vs-subagent split.
- Rollout or log totals are cross-checked against `threads.tokens_used`;
  discrepancies and any scaling needed to reconcile a split to that terminal
  total are recorded, not silently hidden.

For OpenCode, the selected root and descendant sessions are counted once.
Assistant-message token categories are disjoint: raw input, cache read, cache
write, output, and reasoning. Normalized input is raw input plus both cache
categories; normalized total adds output and reasoning. Model/provider/variant
and provider-reported cost are aggregated from assistant messages, because a
mutable `session.model` row cannot attribute lifetime counters after an
in-session model switch. Session counters are used only as a total cross-check
or as an explicit legacy fallback when message usage is unavailable.

## 3. Cost estimate

`cost_usd = sum over models`:

- exact tokens: `non_cached_input × input_price + cached_input ×
  cached_input_price + output × output_price`, all per 1M tokens;
- fallback tokens (no split): `tokens × (input_share × input_price +
  (1 − input_share) × output_price)`.

Prices come only from `evaluation/config/cost_metadata.json` (centralized,
editable). Rules:

- Model lookup is case-insensitive; the metadata file lists aliases
  (`blsc/deepseek-v4-flash`, `deepseek/deepseek-v4-flash`, ...).
- A model without an entry falls back to `defaults` and is flagged as
  `unpriced`; the total of unpriced tokens is reported separately.
- The estimate is always reported with `"estimate": true`; the metadata note
  is carried into the output.

## Output

`expenses.json` (also embedded in `summary.json`):

```jsonc
{
  "workspace": "...",
  "time_seconds": { "goal_time": 0, "wall_time": 0, "by_root_thread": {...} },
  "by_root_tree": { "<root>": { "status": "...", "goal_time_seconds": 0,
                   "goal_tokens": 0, "threads": 0, "tokens_used": 0,
                   "model": "..." } },
  "tokens": {
    "total": 0,
    "by_model": { "gpt-5.6-terra": { "input": 0, "cached_input": 0,
                  "output": 0, "reasoning_output": 0, "total": 0,
                  "fallback_tokens": 0 } },
    "by_thread": { "<thread_id>": { "model": "...", "is_subagent": true,
                  "tokens": {...}, "source": "logs|threads" } },
    "main_vs_subagent": { "main": 0, "subagent": 0 },
    "discrepancy_notes": []
  },
  "cost_estimate_usd": { "total": 0.0, "by_model": {...},
    "unpriced_tokens": 0, "estimate": true,
    "metadata": "evaluation/config/cost_metadata.json" },
  "provenance": { "state_db": "...", "goals_db": "...", "logs_db": "...",
    "sessions_root": "...", "fetched_at": "..." }
}
```

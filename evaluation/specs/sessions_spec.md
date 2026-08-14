# Session Discovery and Bucketed Analysis Specification

## Purpose

Discover every session that belongs to a contestant run from its
**project-isolated `.sessions/` bundle** and produce 30-minute-bucketed
statistics over the merged timeline of the main thread/session and all
subagents: cache-hit history, token usage, shell-call categories, and tool-call
statistics. Whole-length statistics remain available. Idle periods (no events
in any thread/session) are excluded; permission-blocked idle is recognized
heuristically and its limitations are reported.

Produced by `evaluation/tools/extract_sessions.py` (cfdeval package:
`cfdeval.sessions`), embedded in the snapshot as `sessions.json` (schema:
`evaluation/schemas/sessions.schema.json`).

## Discovery (script + agent)

The script discovers candidate **sources**; the evaluation agent classifies
which sessions belong to the run and answers any ambiguity:

| Source | Location | Harness |
|---|---|---|
| `project_codex` | `<workspace>/.sessions/codex/` (state_5.sqlite + sessions/) | codex |
| `project_opencode` | `<workspace>/.sessions/opencode-data/opencode/opencode.db` | opencode |

- No evaluator-account store is searched and no external telemetry path is
  accepted. Missing project-local evidence is unavailable; discovery fails
  closed rather than falling back.
- The agent manually classifies the primary run, then passes
  `--harness codex|opencode`. When both bundled harness stores contain
  candidates and no choice is recorded, `sessions.json.selection.questions`
  surfaces a structured question; `--session-answers <json>` records the
  answer and `summarize.py --session-answers` forwards it.
- Codex roots are scoped with `--roots <thread-id,...>` (each root plus its
  spawn tree), matching the expenses/measurements extractors.
- Docker rows may record cwd `/workspace` although `--workspace` names the host
  contestant directory. This mismatch must be handled explicitly with the
  confirmed `--harness` and `--roots`; cwd matching is only candidate evidence.
- Codex SQLite `rollout_path` values are locators, not authority. Rebase each
  selected row to the matching bundled
  `<workspace>/.sessions/codex/sessions/**/rollout-*.jsonl`; never follow an
  absolute path outside `.sessions/`. A missing or ambiguous match is
  unavailable evidence.

## Merged timeline and idle exclusion

- All events from all selected threads/sessions are merged and sorted:
  codex rollouts (`event_msg`, `turn_context`, `response_item` records) and
  opencode messages/parts (`time.created`/`time.completed`, tool parts).
- **Idle gap** = a gap between consecutive merged events longer than
  `idle_gap_seconds` (default 600): no events anywhere means the main
  thread/session AND all subagents were idle, so the gap is excluded.
- Active intervals are the remaining spans; 30-minute buckets (configurable
  `--bucket-seconds`) are window-aligned from the first event.

## Bucketed statistics (per 30-minute bucket)

- `tokens`: input (incl. cached), cached, non-cached, output, reasoning,
  total — from per-submission usage records (codex `last_token_usage`
  deltas; opencode per-message `tokens` with cache reads normalized into
  input).
- `cache`: `cached_tokens`, `input_tokens`, `hit_ratio` per bucket — the
  cache-hit history of the run.
- `tools`: total, by tool name, and by category (shell/editing/files/
  collaboration/web/package/mcp/skill/interaction/planning/other).
- `turns`: task/turn lifecycle counts (codex).
- `active_entities`: threads/sessions with events in the bucket.
- `active_seconds` / `idle_seconds` per bucket (idle already excluded from
  the stats; the fields quantify the excluded time).

## Whole-length statistics

`whole_session_stats` keeps the entire-window aggregates (tokens, cache,
tools, turns, activity wall/active/idle, per-entity summaries). Token totals
carry accounting notes. Each selected Codex thread, including the primary root
and every selected subagent thread, has its own cumulative usage counter and is
counted exactly once in the run total. Bucket totals are reconstructed from
successive `total_token_usage` deltas within each thread; this retains
per-submission cache history without double-counting repeated streaming ticks.
OpenCode sums its selected session columns in `total_from_all_sessions`.

## Permission-blocked idle

Codex rollouts on this host contain **no explicit approval-request/response
event types**, so permission waits cannot be distinguished from user-away time
with certainty. The analyzer therefore:

1. marks gaps that follow an `ask`-family tool call or an opencode message
   finished with `ask` as **high-confidence permission-wait candidates**;
2. marks gaps between a tool/turn boundary and a user message/new turn in
   approval-aware threads (`approval_policy` != `never` or
   `approvals_reviewer: user`) as **candidates**;
3. records observed policies and a `limitations` list that the evaluation
   agent should read before treating candidates as facts.

`permission_waits.explicit_approval_events_found` is false for codex; ask-tool
events are counted separately (`ask_tool_events_found`).

## Env snapshot pointer

`env_snapshot` records whether `<workspace>/.eval/env_snapshot.json` was
captured before the run (see `env_snapshot_spec.md`); the pipeline copies the
file into the snapshot folder when present.

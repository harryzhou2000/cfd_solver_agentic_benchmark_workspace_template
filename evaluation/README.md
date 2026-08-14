# Benchmark Evaluation — Final Result Summary

Spec files and utilities for producing the **final result summary** of a
benchmark contestant execution, per the evaluation standard defined in the
benchmark repository (`cfd_solver_agentic_benchmark/examiner/`,
`OUTPUT_CONTRACT.md`, `report_requirements.tex`, `SCORING_RUBRIC.md`).

Nothing here writes into the benchmark repository or contestant workspaces;
all inputs are read-only and outputs land under `evaluation/outputs/`.

## What is extracted

| Area | Spec | Config / data |
|---|---|---|
| 1. Expenses: time, tokens (incl. subagents), cost estimate | [specs/expenses_spec.md](specs/expenses_spec.md) | codex `state_5.sqlite`, `goals_1.sqlite`, `logs_2.sqlite`, session rollouts; [config/cost_metadata.json](config/cost_metadata.json) |
| 2. Code review points | [specs/code_review_spec.md](specs/code_review_spec.md) | [config/review_points_code.json](config/review_points_code.json) |
| 3. CFD methods & algorithm review points | [specs/cfd_review_spec.md](specs/cfd_review_spec.md) | [config/review_points_cfd.json](config/review_points_cfd.json) |
| 4. Result review (outputs, visualizations, report) | [specs/result_review_spec.md](specs/result_review_spec.md) | [config/review_points_results.json](config/review_points_results.json) |
| 5. Other measurements: tool usage, LOC, rule violations | [specs/measurements_spec.md](specs/measurements_spec.md) | session rollouts, git, filesystem scan |
| 6. Execution metadata: harness, models/effort, context, subagent types, opencodex router, prompts, AGENTS.md/codegraph/submodule state | [specs/metadata_spec.md](specs/metadata_spec.md) | workspace `.sessions/` telemetry/config bundle and workspace git |
| 7. Verbose config snapshot: captured codex/opencode/opencodex configs and plugin manifests (redacted) | [specs/configs_spec.md](specs/configs_spec.md) | workspace `.sessions/` config bundle only |
| 8. Session discovery + 30-min-bucketed analysis: cache history, tokens, tool categories, idle exclusion, permission waits | [specs/sessions_spec.md](specs/sessions_spec.md) | workspace `.sessions/codex/` or `.sessions/opencode-data/opencode/opencode.db` |
| 9. Environment snapshot captured before the agent runs (optional) | [specs/env_snapshot_spec.md](specs/env_snapshot_spec.md) | host/container, tool versions, proxy env, workspace git state |
| 10. Agent-driven evaluation report + independent rubric, review-area, and eight per-case 0-5 scores stored in the snapshot | [specs/agent_evaluation_spec.md](specs/agent_evaluation_spec.md) | `agent_report.md`, `agent_scores.json` |

Root spec: [specs/summary_spec.md](specs/summary_spec.md). Output schema:
[schemas/summary.schema.json](schemas/summary.schema.json).

## Evaluation result contract

Each contestant run produces a **self-contained result folder** under
`evaluation/outputs/<contestant>/` per [contract/README.md](contract/README.md):
standardized JSON artifacts (each with a schema in `schemas/`), derived MD
renderings, and an `index.json` manifest with sha256 digests for
format-checking.

## Centralized Python module (uv)

The centralized `cfdeval` package (`src/cfdeval/`) owns metadata extraction,
recording (index manifest), format-checking, and querying:

```bash
cd evaluation
uv sync                                   # creates .venv, installs cfdeval
uv run cfdeval check outputs/codex_gpt56_01
uv run cfdeval query table --json
uv run cfdeval query get codex_gpt56_01 expenses.tokens.total
```

The `tools/` scripts are thin wrappers and work with plain `python3` too
(no install required): `extract_metadata.py`, `extract_expenses.py`,
`extract_measurements.py`, `summarize.py`, `check_result.py`.

## Usage

```bash
python3 evaluation/tools/summarize.py --workspace ../codex_gpt56_01
```

This runs the whole pipeline and writes
`evaluation/outputs/codex_gpt56_01/`:

- `summary.json` — machine-readable final summary (schema-checked keys);
- `summary.md` — human-readable report;
- `expenses.json`, `measurements.json`, `metadata.json` — detail per area;
- `configs.json` — verbose redacted config stack (see
  [specs/configs_spec.md](specs/configs_spec.md));
- `sessions.json` — session discovery + bucketed analysis (see
  [specs/sessions_spec.md](specs/sessions_spec.md));
- `env_snapshot.json` — environment snapshot (only when the workspace has
  `.eval/env_snapshot.json`; see [specs/env_snapshot_spec.md](specs/env_snapshot_spec.md));
- `agent_scores.json` + `agent_report.md` — agent-driven evaluation (see
  [specs/agent_evaluation_spec.md](specs/agent_evaluation_spec.md));
- `review_code.md|json`, `review_cfd.md|json`, `review_results.md|json` —
  blank scorecards for reviewers.

Individual steps:

```bash
python3 evaluation/tools/extract_expenses.py --workspace ../codex_gpt56_01
python3 evaluation/tools/extract_measurements.py --workspace ../codex_gpt56_01
python3 evaluation/tools/extract_metadata.py --workspace ../codex_gpt56_01
python3 evaluation/tools/extract_configs.py --workspace ../codex_gpt56_01
python3 evaluation/tools/extract_sessions.py --workspace ../codex_gpt56_01
python3 evaluation/tools/generate_review_forms.py --out evaluation/outputs/codex_gpt56_01
```

All telemetry and captured run-time configuration is read exclusively from
`<workspace>/.sessions/`. The pipeline never consults the evaluator account's
session/config state and rejects path overrides outside that boundary. Missing
local evidence is reported as unavailable rather than filled from a fallback.
Use `--harness codex|opencode` after manual classification, `--roots` for the
confirmed Codex root tree, and `--session-answers` only for recorded
classification answers.

## Environment snapshot (before the agent runs)

Capture the real environment before starting a contestant agent (vendored
into `docker/scripts/setup-workspace.sh`, so new workspaces get it
automatically; optional for older runs):

```bash
python3 evaluation/tools/env_snapshot.py --workspace ../omo_slim_dsv4_01 --probe-proxy
```

Writes `<workspace>/.eval/env_snapshot.json` (git-excluded). The pipeline
copies it into the snapshot as `env_snapshot.json`; runs without it record
`env_snapshot.captured: false`.

## Session analysis (bucketed)

`sessions.json` merges the main session/thread with all subagents and reports
**30-minute buckets** of cache-hit history, token usage, shell-call
categories, and tool statistics, with idle periods (no events in any
thread/session beyond the gap threshold) excluded. Whole-length statistics
remain in `whole_session_stats`. Permission-blocked idle is recognized
heuristically (ask-tools, approval-aware settings) with explicit limitations —
see [specs/sessions_spec.md](specs/sessions_spec.md).

## Agent-driven evaluation

The evaluation agent fills reports and scores inside the snapshot:

```bash
python3 evaluation/tools/generate_agent_report.py \
  --out evaluation/outputs/codex_gpt56_01 --workspace ../codex_gpt56_01 \
  --run-validator --agent gpt-5.6-terra
# ... agent fills agent_scores.json + agent_report.md ...
python3 evaluation/tools/record_agent_results.py \
  --folder evaluation/outputs/codex_gpt56_01
```

See [specs/agent_evaluation_spec.md](specs/agent_evaluation_spec.md).

## GUI

Local web GUI listing snapshots with key results and interactive detail views
(identity, scores/DQ, execution date, summary, evaluator and contestant reports,
bucketed sessions, metadata, configs, env). It can display a report PDF found
in the matching `workspace/` repo in place; it does not serve PNGs or CFD field
artifacts:

```bash
python3 evaluation/gui/server.py --port 8787
# open http://127.0.0.1:8787
```

Read-only; stdlib only. Override workspace discovery with
`--workspace-root <path>` when the manager's `workspace/` directory is
elsewhere. The PDF endpoint is deliberately constrained to that root.

## Session selection

Candidate discovery is limited to the workspace `.sessions/` bundle. The
evaluator must manually identify the primary harness and root/session tree;
cwd matching alone is insufficient. `summary.json`/`expenses.json` report a
per-root-session breakdown
(`time_seconds.by_root_tree`: status, model, thread count, tokens, goal time),
and `--roots <thread-id,...>` scopes the whole pipeline to the chosen root
session(s) and their subagent trees, e.g.:

```bash
python3 evaluation/tools/summarize.py --workspace ../codex_gpt56_01 \
  --harness codex \
  --roots 019fb9e3-ba6e-7e40-99d3-84d723942dc8
```

For Docker-isolated runs, `--workspace` is the host contestant repository even
when database rows record `/workspace`. The extractor rebases Codex SQLite
`rollout_path` values onto bundled `.sessions/codex/sessions/**/rollout-*.jsonl`;
it never follows an original absolute path. OpenCode always uses
`.sessions/opencode-data/opencode/opencode.db`.

## Missing metadata and classification questions

`extract_metadata.py` never guesses. When a metadata field cannot be extracted
(e.g. opencode session data is unavailable, a model is missing from the
catalog, or reasoning effort is not recorded for a router-managed model), it
emits a structured `questions` entry, sets `status: needs_user_input`, and
lists the questions in `summary.md`. Operator answers may resolve manual
harness/root classification or document a limitation, but they do not replace
missing `.sessions` telemetry or authorize another data source. Record answers
(`{"<question_id>": "..."}`) and re-run with `--answers` when appropriate:

```bash
python3 evaluation/tools/summarize.py --workspace ../opencode_omoslim_deepseek \
  --answers evaluation/outputs/opencode_omoslim_deepseek/metadata_answers.json
```

For opencode contestants the pipeline extracts harness/version, sessions
(roots + subagents, model + reasoning variant), tokens/cost per session, and
prompts from the opencode database; codex-only expense/measurement extractors
are skipped for those runs. If the canonical bundled OpenCode database is
absent, these fields remain unavailable and extraction fails closed.

### Session activity time (opencode)

Wall-clock session time is not a fair measure of work: sessions are frequently
interrupted by the user (away from keyboard) or by API stalls. For opencode
runs the pipeline therefore computes **activity time** per session from the
message history (`time.created` / `time.completed` events): gaps between
consecutive events longer than the idle threshold count as interrupted idle
time and are excluded from activity time. Reported as
`opencode.activity_time_seconds` (and per-session
`sessions[].activity.activity_time_seconds`, with wall/idle breakdown and the
number of idle gaps).

The threshold defaults to 600 s and can be tuned per run:

```bash
python3 evaluation/tools/summarize.py --workspace ../omo_slim_dsv4_01 \
  --idle-gap-seconds 300
```

Codex data paths are fixed beneath `<workspace>/.sessions/codex/`; explicit
external overrides are rejected. If a database or bundled rollout is absent,
the corresponding metadata is unavailable and the pipeline fails closed.

## Publishing a contestant's results

When a contestant run is complete (marked with the `done` file), its
self-contained result lives on a regulated `results/<slug>` branch. The
publish helper prepares and pushes that branch for a contestant workspace:

```bash
python3 evaluation/tools/publish_results.py --workspace ../omo_slim_dsv4_01 \
  --dry-run          # preview every action, change nothing
python3 evaluation/tools/publish_results.py --workspace ../omo_slim_dsv4_01 \
  --push             # restore origin, fetch full history, branch, commit, push
```

What it does, in order (each step is printed):

1. Restores the `origin` remote if missing (default URL: this manager
   repository's origin, i.e. the workspace template repo; override with
   `--origin-url`).
2. Fetches the full remote history — contestant clones are usually **shallow**,
   and remotes reject shallow pushes, so it runs `git fetch --unshallow origin`
   first when needed, then `git fetch --all --tags --prune`.
3. Moves to a regulated branch named `results/<slug>` (default slug = the
   workspace directory name, normalized to lowercase hyphens; `--branch`
   overrides). It creates the branch from the current branch, or renames the
   current branch with `--rename-current`; existing targets are switched to.
4. Commits the whole working tree (`git add -A`, default message
   `results: <slug>`); skip with `--exclude GLOB` (repeatable) for large
   artifacts you do not want in the branch.
5. Pushes with `git push -u <remote> <branch>` — **only when `--push` is
   given**; the default remote is `origin` (`--push-remote` overrides, add an
   unconfigured remote with `--push-url`). Without `--push` the helper only
   prepares the local branch and prints the push command it would run.

The helper is the mechanical front-end for the "results branch" contract in
[contract/README.md](contract/README.md); run it with `--dry-run` first, then
with `--push` once the preview matches what you want to publish.

## Data sources (codex contestants, for now)

- `state_5.sqlite` — `threads` (per-session model, `tokens_used`, `cwd`,
  rollout path), `thread_spawn_edges` (subagent tree).
- `goals_1.sqlite` — `thread_goals.time_used_seconds` (goal time, read
  directly) and per-goal tokens.
- `logs_2.sqlite` — per-turn `codex.turn.token_usage.*` records with exact
  input / cached-input / output / reasoning-output token splits per model.
- Session rollouts (`sessions/**/rollout-*.jsonl`) — timestamps, tool calls,
  subagent messages, sandbox/approval context, and rule-violation evidence.

## Costs

Costs are **estimates**: tokens × prices in
[config/cost_metadata.json](config/cost_metadata.json). Replace the seeded
prices with actual provider/contract pricing before using the numbers outside
this evaluation. Models without an entry are flagged as unpriced and fall back
to the file's `defaults`.

## Review workflow

1. `summarize.py` fills everything automatically extractable; all review
   scores start as `null`.
2. Reviewers score each point 0–5 in the generated `review_*.md` scorecards
   and mark disqualification flags.
3. Record scores back into the `review_*.json` sidecars; overall score is the
   weighted mean of point scores.

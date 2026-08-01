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

Root spec: [specs/summary_spec.md](specs/summary_spec.md). Output schema:
[schemas/summary.schema.json](schemas/summary.schema.json).

## Usage

```bash
python3 evaluation/tools/summarize.py --workspace ../codex_gpt56_01
```

This runs the whole pipeline and writes
`evaluation/outputs/codex_gpt56_01/`:

- `summary.json` — machine-readable final summary (schema-checked keys);
- `summary.md` — human-readable report;
- `expenses.json`, `measurements.json` — detail per area;
- `review_code.md|json`, `review_cfd.md|json`, `review_results.md|json` —
  blank scorecards for reviewers.

Individual steps:

```bash
python3 evaluation/tools/extract_expenses.py --workspace ../codex_gpt56_01
python3 evaluation/tools/extract_measurements.py --workspace ../codex_gpt56_01
python3 evaluation/tools/generate_review_forms.py --out evaluation/outputs/codex_gpt56_01
```

All codex data paths (`~/.codex/state_5.sqlite`, `goals_1.sqlite`,
`logs_2.sqlite`, `sessions/`) are overridable via `--state-db`, `--goals-db`,
`--logs-db`, `--sessions-root`, so the same tools work against a snapshot of
another machine's `~/.codex`.

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

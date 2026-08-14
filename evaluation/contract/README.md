# Evaluation Result Contract v1.0

This contract standardizes the **self-contained evaluation result folder**
produced for each benchmark contestant run. A result folder is the single
unit of evaluation evidence: everything about a run is inside it, in
machine-readable JSON (standardized by schema) and human-readable MD
(derived renderings).

## Folder layout

`evaluation/outputs/<contestant>/`:

```text
index.json              # manifest: contract version, artifacts + schemas + sha256
summary.json            # canonical summary (embeds the areas below)
summary.md              # human-readable rendering of summary.json
metadata.json           # harness, models, context, subagents, router, prompts,
                        # workspace state (AGENTS.md, codegraph, submodule)
expenses.json           # time, tokens, cost estimate
measurements.json       # tool usage, LOC, rule violations
configs.json            # verbose redacted config stack (codex/opencode/opencodex,
                        # plugins, shell init, workspace-local, benchmark task/rubric)
sessions.json           # session discovery (system + project-isolated) and
                        # 30-min-bucketed analysis (cache history, tokens,
                        # tool categories, idle exclusion, permission waits)
env_snapshot.json       # pre-run environment evidence, or explicit post-run
                        # legacy provenance reconstruction
agent_scores.json       # agent-driven evaluation scores (review areas + rubric)
agent_report.md         # agent-driven evaluation report (narrative + evidence)
run_identity.json       # canonical run ID and immutable Git identity sidecar
contestant_final_response.md # exact attributed terminal response sidecar
review_code.json|md     # code review scorecard
review_cfd.json|md      # CFD methods review scorecard
review_results.json|md  # result review scorecard (+ structural evidence)
```

## Standardization rules

1. **Every JSON artifact validates against its schema** in
   `evaluation/schemas/` (`summary.schema.json`, `metadata.schema.json`,
   `expenses.schema.json`, `measurements.schema.json`, `review.schema.json`,
   `index.schema.json`). The schema version is recorded per artifact in
   `index.json`.
2. **`index.json` is the integrity manifest**: it lists every artifact with
   its schema reference and sha256 digest. A folder is contract-conformant
   only if all digests match.
3. **MD files are derived renderings** of the JSON artifacts — never edited
   by hand. Scores and notes are recorded in the `review_*.json` sidecars and
   rendered into the MD scorecards.
4. **Nothing writes into the benchmark repo or contestant workspaces**; the
   result folder is generated read-only from those sources.
5. **Unextractable metadata is recorded as questions**, not guesses
   (`metadata.questions` + `status: needs_user_input`); user answers are
   merged via `--answers` and persisted in `metadata.user_answers`.
6. **Configs are captured verbosely but redacted** (`configs.json`): every
   bundled config that governed the run (codex/opencode/opencodex, plugin
   manifests, workspace-local, benchmark task/rubric) is recorded
   with content where safe. Credential files (`auth.json`,
   `codex-accounts.json`, `admin-api-token`) are presence + sha256 only;
   secret-like values are replaced with `***REDACTED***`.
7. **Session analysis is bucketed and idle-excluded** (`sessions.json`):
   discovery covers only telemetry bundled below the contestant workspace's
   `.sessions/` directory; statistics are reported in 30-minute buckets
   (cache-hit history, tokens, shell-call categories, tool stats) with idle
   periods excluded and whole-length stats preserved. Permission-blocked
   idle is recognized heuristically and reported with its limitations.
8. **Agent-driven evaluation is part of the snapshot**: the evaluating agent
   fills `agent_scores.json` (review-area scores + 100-point rubric +
   disqualification flags + metadata answers + session selection) and writes
   `agent_report.md`; `record_agent_results.py` validates and re-indexes.

## Fill, check, query

Fill (generates the folder + `index.json`):

```bash
python3 evaluation/tools/summarize.py --workspace ../codex_gpt56_01
# or, with the cfdeval package:
cd evaluation && uv run cfdeval summarize --workspace ../codex_gpt56_01
```

Format-check (schemas + sha256 manifest):

```bash
uv run cfdeval check evaluation/outputs/codex_gpt56_01
python3 evaluation/tools/check_result.py evaluation/outputs/codex_gpt56_01
```

Query:

```bash
uv run cfdeval query list                       # result folders + status
uv run cfdeval query table --json               # comparison table
uv run cfdeval query show codex_gpt56_01        # summary.md
uv run cfdeval query get codex_gpt56_01 expenses.tokens.total
uv run cfdeval query get codex_gpt56_01 metadata.workspace.benchmark_submodule.commit
```

Scoring: reviewers edit `review_*.json` scores, then re-render
(`generate_review_forms.py` refreshes the MD from the JSON sidecars).

## Contract versioning

`index.json.contract_version` identifies the contract revision that produced
the folder. Schema files are the normative definitions; contract changes bump
the version and keep old folders readable (schemas are backward-compatible or
versioned by filename).

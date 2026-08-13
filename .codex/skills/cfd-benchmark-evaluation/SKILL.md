---
name: cfd-benchmark-evaluation
description: Evaluate CFD benchmark contestant workspaces from the manager repository. Use for pre-evaluation result-branch commits, canonical run-ID derivation, manager-side evidence snapshots, structural validator runs, source and numerical review, 100-point rubric scoring, review comments, disqualification assessment, result recording, integrity checks, and cross-contestant comparison.
---

# CFD Benchmark Evaluation

Produce an evidence-backed, self-contained evaluation under
`evaluation/outputs/<run-id>/`. Treat the contestant workspace as the source of
submission evidence and the manager repository as the durable evaluation
record.

## Non-negotiable boundaries

- Establish submission provenance before inspecting or scoring it.
- After the result commit, evaluate the contestant workspace read-only. Write
  evaluation artifacts only in the manager repository.
- Derive identity from the initial branch recorded before the agent ran. Never
  infer identity from models found in sessions or telemetry.
- Require the operator to supply the run number. Preserve it byte-for-byte,
  including leading zeros; never auto-increment or normalize it.
- Keep structural validation separate from source, physics, MPI, and report
  judgment. A passing validator is necessary, not sufficient.
- Use `null` plus a limitation for anything not verified. Do not turn missing
  evidence into a favorable or unfavorable guess.
- Treat failed, aborted, or partial histories as such. Do not call them
  converged results.

## 1. Resolve immutable run identity

Require:

- contestant workspace path;
- operator-selected number, such as `08`;
- `<workspace>/.eval/env_snapshot.json` from before the contestant ran.

The pre-run snapshot is authoritative for:

- `initial_branch = workspace.branch`;
- `initial_commit = workspace.commit`.

The initial branch must be exactly `<harness>/<model>/init`. Remove only the
final `init` component to obtain the result-branch prefix. For example:

```text
initial branch: codex/gpt56/init
number:         08
result branch:  codex/gpt56/08
run-id base:    codex_gpt56_08
```

Do not use the workspace basename. Do not replace `gpt56` with a model
actually observed during the run.

If the pre-run snapshot is absent or lacks branch/commit, stop and ask the
operator for an explicit provenance decision. Do not silently use the current
branch or current commit as the initial state.

Do not finalize a run ID from working-tree or index bytes. First create the
result commit as described in step 2. Then run `scripts/derive_run_id.py`
against that immutable commit. It selects 1–3 stable submission artifacts from
the commit tree, hashes their Git blob bytes, and writes the exact identity
record:

```bash
python3 .codex/skills/cfd-benchmark-evaluation/scripts/derive_run_id.py \
  --workspace <workspace> --number 08 \
  --submission-commit <result-commit-sha> \
  --out /tmp/cfd-run-identity.json
```

The canonical run ID is:

```text
<harness>_<model>_<operator-number>_<state-hash-6>
```

The state hash is the first six lowercase hex characters of SHA-256 over
canonical UTF-8 lines containing the full initial commit object ID and each
selected path-qualified artifact SHA-256. Artifact SHA-256 values are computed
from blob bytes read from the immutable result commit, never from filesystem
files. Paths are relative to the repository and sorted. Thus identical content
at different artifact paths does not alias, while checkout metadata, mtimes,
file ownership, line-ending conversion, and later worktree edits cannot change
the run ID.

The submission commit SHA is provenance and an immutable lookup boundary; it
is deliberately **not** an input line in the state hash. Commit message,
author, committer, timestamp, parent topology, and unrelated files therefore
do not change the run ID when the initial commit and selected path/content
pairs are identical. A different submission commit changes the run ID only if
one of the selected artifact blobs or paths changes.

By default the helper selects up to three stable, tracked submission files in
this priority order:

1. `solver/CMakeLists.txt` or top-level `CMakeLists.txt`;
2. `report/report.tex` (including documented standard alternate locations);
3. `report/run_manifest.csv`, falling back to `report/run_manifest.md`.

Use repeatable `--artifact RELPATH` to select explicit stable files when the
layout is non-standard. The helper requires all selected paths to be regular
file blobs in the result commit. Require 1–3 files. Never use logs,
timestamps, generated result files, build products, session databases,
`.eval`, `.sessions`, `done`, or the current Git commit as an artifact input.

Rerun the helper with the full recorded result commit SHA and compare the
identity-bearing fields (`canonical_record`, state hash, run ID, commits, and
artifact entries). Absolute diagnostic paths such as `workspace` and
`env_snapshot` may differ across equivalent checkouts and are not hash inputs.
For verification, pass the recorded artifact paths back as explicit repeated
`--artifact` options; do not redo default selection, because a later skill
version may adopt different defaults. Honor `canonicalization_version: 1` for
existing identities.
Copy the final record to
`evaluation/outputs/<run-id>/run_identity.json` after `summarize.py` creates
the snapshot. Identical immutable identity inputs must produce the same
canonical record and hash. Treat a mismatch as an integrity failure; do not
silently issue another ID for the same commit.

## 2. Commit the contestant submission before evaluation

Use the pre-run snapshot's `initial_branch` and `initial_commit` to determine
the result branch. Before changing branches, record the current branch, HEAD,
status, and submodule state in the evaluation notes.

Then create or switch to the exact result branch and commit the complete
submission:

```text
<initial branch without /init>/<operator number>
```

For example, use `codex/gpt56/08`, not `results/codex-gpt56-08`.

Requirements:

- Refuse an existing target branch unless its intended reuse is explicitly
  confirmed and its history is compatible with the recorded initial commit.
- Stage the complete contestant submission, including solver, results, report,
  `done`, and relevant tracked submodule pointers.
- First derive the canonical submission set from the task/output contract and
  the contestant's documented final paths. Separate final results from probes,
  tuning experiments, debug histories, object files, and abandoned outputs.
  If that distinction is not evident from committed documentation and final
  manifests, stop and ask the operator; do not guess which artifacts are
  canonical.
- Do not stage ignored session/config telemetry, credentials, build trees, the
  `external` symlink target, or `.eval` scratch data.
- Never use blind `git add -A` in a dirty workspace. Stage the audited
  canonical paths explicitly, then use `git status --short --untracked-files=all`
  to account for every remaining untracked path as excluded or unexpectedly
  missing from the submission.
- Inspect the staged file list and diff-stat before committing.
- Use commit message `results: <result-branch>`, so the operator number appears
  exactly as it does in the branch and run ID.
- Record the resulting submission commit SHA. Verify the worktree is clean,
  aside from explicitly documented ignored or excluded material.
- Do not push unless the user separately authorizes pushing.

After committing, call the helper with the full submission commit SHA. Verify
that both the initial and submission revisions resolve to local Git commit
objects and that the submission commit is the exact tip of the intended result
branch. The run ID describes immutable blobs in that committed submission,
while `initial_commit` preserves the starting template state.

## 3. Create the manager-side snapshot

Run the automated extraction with an explicit output path:

```bash
python3 evaluation/tools/summarize.py \
  --workspace <workspace> \
  --out evaluation/outputs/<run-id>
```

Never allow the default workspace-basename output naming. Store or verify in
the snapshot:

- canonical run ID and its derivation record;
- initial branch and initial commit;
- result branch and submission commit;
- operator number;
- selected artifact paths and SHA-256 values;
- benchmark submodule commit;
- session selection and rationale;
- metadata questions and operator answers;
- configs and environment with secrets redacted;
- scores, comments, evidence, limitations, and disqualification findings.

The snapshot is independent of the contestant workspace. Do not rely on the
workspace remaining available after recording.

`run_identity.json` is the authoritative identity/provenance sidecar even in
manager revisions whose `index.json` schema does not yet enumerate it. Record
its SHA-256 in `agent_report.md` and flag the indexing limitation explicitly;
do not imply that `cfdeval check` validated an unindexed sidecar.

## 4. Validate and inspect

Read these authorities before scoring:

- `cfd_solver_agentic_benchmark/TASK.md`;
- `cfd_solver_agentic_benchmark/OUTPUT_CONTRACT.md`;
- `cfd_solver_agentic_benchmark/examiner/README_EXAMINER.md`;
- `cfd_solver_agentic_benchmark/examiner/SCORING_RUBRIC.md`.

Run `validate_outputs.py` explicitly with all eight required final case
directories and the report directory. Do not rely on the current
`generate_agent_report.py --run-validator` hook unless its constructed command
has been verified; older revisions called the validator without its required
arguments.

Also perform the manual examiner workflow:

- clean documented build and required CLI checks;
- source inspection for actual numerical methods and prohibited shortcuts;
- serial and MPI rank-count execution/comparison where feasible;
- output-contract and final-state consistency;
- residual, force, positivity, wall, shock, wake, and transient plausibility;
- field-file and report-figure inspection;
- traceability of claims and figures to submitted data;
- all disqualification triggers.

Capture exact commands, return codes, relevant output, file paths, and line
references. Distinguish a validator failure, numerical failure, infrastructure
failure, timeout, and evaluator interruption.

## 5. Score and comment

Generate the evaluation scaffold without trusting it as completed evaluation:

```bash
python3 evaluation/tools/generate_agent_report.py \
  --out evaluation/outputs/<run-id> \
  --workspace <workspace> --agent <evaluator-name>
```

Fill both:

- `agent_scores.json`: review-area points, ten-section 100-point rubric,
  disqualification evidence, session selection, metadata answers, limitations;
- `agent_report.md`: methodology, evidence, findings, comments, limitations,
  and verdict.

Every deduction needs concrete evidence. Every full-credit claim needs enough
evidence to show it was verified, not merely self-reported. Do not
proportionally extrapolate a partial rubric as if it were a completed score;
leave the total incomplete until all required sections are assessed.

## 6. Record, validate, and hand off

Record the completed evaluation:

```bash
python3 evaluation/tools/record_agent_results.py \
  --folder evaluation/outputs/<run-id>
cd evaluation && uv run cfdeval check outputs/<run-id>
```

Inspect `index.json` and confirm all expected JSON artifacts are indexed with
matching SHA-256 values. Confirm comparison queries show the run under its
canonical run ID and that review and rubric scores are non-null only where the
evaluation is complete.

Commit the manager-side snapshot separately from the contestant submission.
Report both commit SHAs, the exact run ID, result branch, snapshot path,
validator status, rubric status, disqualification status, and limitations.

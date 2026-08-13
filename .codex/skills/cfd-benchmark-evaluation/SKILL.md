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
- Prove the exact result branch is absent from the configured upstream before
  creating it. A failed or unavailable remote check is a blocker, not evidence
  that the number is free.
- Commit only reproducible code/report material. Never commit raw solver data,
  logs, restarts, field files, or visualization working files. Permit curated
  PNGs only under the report's figure directory so the report builds directly
  from the repository.
- Keep structural validation separate from source, physics, MPI, and report
  judgment. A passing validator is necessary, not sufficient.
- Trust that submitted code and artifacts are the contestant's work. Do not
  perform internet-copy, authorship, or plagiarism investigation. Still check
  that claims match the submitted source and that the solver actually runs.
- Separate report craftsmanship, submitted-result completeness, and solver
  functionality. Evaluator-generated plots or reruns may clarify solver
  behavior but never repair the contestant's report or missing deliverables.
- Never rerun the Re 200 unsteady case. Judge its completeness and credibility
  only from readable contestant deliverables.
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
canonical UTF-8 lines containing the full initial commit object ID, the full
submission commit object ID, and each selected path-qualified artifact
SHA-256. Artifact SHA-256 values are computed from blob bytes read from the
immutable result commit, never from filesystem files. Paths are relative to
the repository and sorted. Thus identical content at different artifact paths
does not alias, while checkout metadata, mtimes, file ownership, line-ending
conversion, and later worktree edits cannot change the run ID.

The full submission commit object ID is also an input line in the state hash.
This binds the run ID to the exact curated submission commit, including all
committed solver/report changes rather than only the 1–3 sampled artifacts.
The sampled artifact hashes remain independent, human-auditable state
evidence. Rewriting the submission commit creates a different run ID even when
the sampled files are unchanged; the same immutable commit always reproduces
the same run ID.

By default the helper selects up to three stable, tracked submission files in
this priority order:

1. `solver/CMakeLists.txt` or top-level `CMakeLists.txt`;
2. a primary tracked solver source file from documented standard locations;
3. `report/report.tex` (including documented standard alternate locations).

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
version may adopt different defaults. New identities use
`canonicalization_version: 2`, which includes both initial and submission
commit IDs. Preserve version 1 records only as legacy identities; never
silently reinterpret them as version 2.
Copy the final record to
`evaluation/outputs/<run-id>/run_identity.json` after `summarize.py` creates
the snapshot. Identical immutable identity inputs must produce the same
canonical record and hash. Treat a mismatch as an integrity failure; do not
silently issue another ID for the same commit.

## 2. Commit the contestant submission before evaluation

Use the pre-run snapshot's `initial_branch` and `initial_commit` to determine
the result branch. Before changing branches, record the current branch, HEAD,
status, and submodule state in the evaluation notes.

Before creating or switching to the branch, check the exact upstream ref:

```bash
python3 .codex/skills/cfd-benchmark-evaluation/scripts/check_upstream_branch.py \
  --workspace <workspace> --number 08
```

The helper derives the branch from the pre-run initial branch and runs an
exact `git ls-remote --heads` query against the manager repository's `origin`
URL. Continue only when it reports `available: true`. Verify the printed URL
is the canonical workspace-template upstream. An explicit `--upstream` is an
operator-approved override, not an evaluator convenience. Refuse a remote
collision even if the branch is absent locally. Refuse any local branch
collision; result branches are single-use and never reused. Never choose
another number automatically; return to the operator.

Then create the exact new result branch and commit the complete submission:

```text
<initial branch without /init>/<operator number>
```

For example, use `codex/gpt56/08`, not `results/codex-gpt56-08`.

Requirements:

- Refuse any existing target branch locally or upstream. Never switch to or
  reuse it for another evaluation.
- Stage the curated contestant submission: solver source, build/config files,
  reproducibility scripts, report TeX/bibliography sources, curated report
  PNGs, `done`, and relevant tracked submodule pointers. Embed small report
  tables in report source; do not commit raw tabular data files.
- First derive the canonical submission set from the task/output contract and
  the contestant's documented final paths. Keep all run data in the workspace
  for evaluation but out of Git. Separate final results from probes, tuning
  experiments, debug histories, object files, and abandoned outputs.
  If that distinction is not evident from committed documentation and final
  manifests, stop and ask the operator; do not guess which artifacts are
  canonical.
- Never stage result directories; residual/force/surface/partition CSV or JSON;
  run metadata/status files; stdout/stderr or other logs; restart/checkpoint
  files; CGNS/VTK/VTU/HDF5/binary field data; visualization exports or working
  files; raw CSV, NumPy, Parquet, Feather, MATLAB, pickle, or database data;
  generated report PDF; executables, libraries, objects, or build/cache trees.
  Existing immutable benchmark inputs inherited from the initial commit need
  not be removed, but the result commit must not add or modify such data.
- Permit `.png` files only below a report `figures/` directory, and only when
  they are curated figures referenced by the committed `report.tex`. Do not
  commit PNGs from probes, debug runs, or general result/visualization
  directories. Build the report from the committed tree after the audit; a
  textual reference check does not prove that the report compiles.
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

After committing, audit the immutable delta against the initial commit:

```bash
python3 .codex/skills/cfd-benchmark-evaluation/scripts/audit_submission_commit.py \
  --workspace <workspace> --submission-commit <result-commit-sha>
```

Do not evaluate or publish if the audit reports a prohibited changed path.
Review the audit's allowed path list too; pattern checks supplement rather than
replace evaluator judgment.

Then call the run-ID helper with the full submission commit SHA. Verify
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

The manager snapshot stores metadata, scores, comments, commands, summarized
evidence, integrity records, and evaluation reports. It is not a second raw
data archive: do not copy solver logs, restart/field files, visualization
working data, or bulk result directories into `evaluation/outputs/<run-id>`.

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

For the copied/open-source-core trigger, apply the trust policy: do not search
the internet or external codebases for similarity. Record only direct internal
evidence such as invoking an existing external solver executable or a wrapper
that is plainly present in the submitted tree.

Capture exact commands, return codes, relevant output, file paths, and line
references. Distinguish a validator failure, numerical failure, infrastructure
failure, timeout, and evaluator interruption.

### Evidence escalation and rerun policy

Assess each case using the least invasive sufficient evidence. Follow this
order and stop as soon as the evidence is decisive:

1. Read the contestant's report, manifests, result files, and stated
   limitations. If the report is already good, explicitly admits a bad or
   incomplete result, or readable submitted results conclusively show failure,
   trust that evidence. Do not rerun merely to reproduce a clear conclusion.
2. If the underlying result files are readable and potentially useful but the
   report figures are missing, poor, misleading, or technically hard to read,
   create evaluator-only plots from those existing files. Use the submitted
   plotting scripts when suitable; otherwise use a small independent plotting
   command. Do not edit `report.tex`, replace report PNGs, or commit generated
   plots. Record what source files and variables were visualized.
3. Only when evidence required to judge solver functionality is genuinely
   missing, consider rerunning a **steady** case. First confirm the source
   builds and identify the exact submitted case/config/mesh and documented
   command. Prefer the cheapest representative missing case and minimum rank
   count sufficient to answer the unresolved question. Do not rerun merely to
   improve presentation or rescue a result already confirmed bad.
4. Never rerun an unsteady case. In particular, never rerun cylinder Re 200,
   shorten it, resume it, or substitute a cheaper transient. Re 200
   completeness, periodicity, controls, and result credit depend exclusively
   on readable contestant deliverables.

Perform evaluator redraws and permitted steady reruns in an isolated scratch
checkout/worktree at the immutable submission commit, with outputs under a
temporary or ignored evaluator directory. Keep the canonical contestant
workspace and committed report unchanged. Do not commit or copy raw rerun
outputs, logs, restarts, fields, or evaluator visualization files into either
the result branch or manager snapshot. Preserve only commands, return codes,
small numeric summaries, and evidence-backed conclusions in the evaluation
report.

Treat contestant build, solver, and plotting commands as untrusted execution.
Run them in the benchmark container or equivalent sandbox with network access
disabled, only the scratch checkout and required immutable benchmark
inputs/externals mounted, and explicit CPU, memory, process-count, and wall-time
limits. Do not expose host credentials, user configuration, manager-repository
write access, or unrelated host paths.

For reruns, use the immutable submitted source without edits, patches,
parameter tuning, extra convergence aids, or case-file repair. Use the exact
submitted input and documented production command where available. Allow at
most one normal attempt per missing steady case; retry once only for a clearly
identified infrastructure failure that did not exercise the solver. Stop when
the unresolved functionality question is answered. Record timeout and resource
limits. If meaningful verification would exceed the configured evaluation
budget, leave it unverified and state the limitation rather than launching an
open-ended run.

Label every piece of evidence as one of:

- `contestant_report` — submitted prose/figures;
- `contestant_result` — readable submitted run artifact;
- `evaluator_redraw` — evaluator visualization of contestant result data;
- `evaluator_rerun` — new steady run produced by the evaluator.

Never present evaluator-generated evidence as contestant-delivered evidence.

### Independent scoring dimensions

Score these dimensions independently:

- **Report and visualization quality:** judge only the contestant's committed
  report, figures, clarity, traceability, and honest limitations. Evaluator
  redraws do not add report points and do not overwrite an explicitly poor
  report assessment.
- **Submitted-result completeness and case-results credit:** judge readable
  contestant deliverables. An evaluator rerun does not retroactively make a
  missing submitted result complete. Re 200 uses this evidence class only.
- **Solver implementation and functionality:** use source inspection,
  contestant results, evaluator redraws, and permitted steady reruns as
  appropriately labeled evidence. A poor report alone must not imply a broken
  solver; conversely, attractive report figures must not substitute for solver
  evidence.

When the report and solver disagree, state separate conclusions, for example:
"solver functionality verified by evaluator steady rerun; contestant result
missing and report receives no completion/visualization credit." Do not hide
this distinction inside a single blended comment.

Use `0` for a criterion when evidence affirmatively establishes that its
required deliverable, behavior, or result is missing, failed, or noncompliant.
Use `null` only when the criterion cannot be verified from available evidence
and no decisive failure is established. A structurally complete divergent
case may retain purely structural output-contract credit while receiving zero
for its physical completion/result criterion.

For Re 200, source inspection may support only implementation-level findings
that a second-order transient method and inner-control machinery exist. It
cannot establish that the contestant ran the production controls, reached the
required horizon, converged inner solves, or obtained periodic shedding. Those
execution/completeness/result findings depend exclusively on readable
contestant deliverables; missing or incomplete deliverables receive zero for
the affected Re 200 result criteria and are never replaced by a rerun.

Apply a missing case independently to each rubric criterion whose own wording
is unmet: output-contract completeness, physical case-result completion, and
report coverage may each be affected. Explain each deduction with that
criterion's evidence; do not apply an extra blanket penalty or reuse one
failure as justification for unrelated method/source deductions. When a rubric
bucket aggregates several cases and prescribes no per-case formula, use a
transparent proportional starting point within that bucket, adjust only for
documented qualitative differences, and state the allocation explicitly.

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

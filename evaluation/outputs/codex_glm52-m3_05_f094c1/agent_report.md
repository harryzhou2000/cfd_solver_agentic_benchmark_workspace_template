# Agent Evaluation Report — codex_glm52-m3_05_f094c1

## Verdict

The immutable curated submission is `0065953716e70f087f5d6d4fb512bc7e2fe2ba97` on `codex/glm52-m3/05`, derived from `codex/glm52-m3/init` at `a14f203babe6794ad032aea760aed8e302e62b5a`. Its 100-point rubric score is 60/100, but it is disqualified: DQ trigger 10 is established by the submitted source. `solver.cpp` sets `startup_steps=100000` and leaves reconstruction limiter `phi` zero in the steady production path, and explicitly zeros `phi` in the transient path. The report simultaneously presents piecewise-linear reconstruction and Venkatakrishnan limiting as the numerical scheme while admitting first-order production operation.

## Provenance and session selection

The result-branch collision check queried the canonical manager upstream and found the exact ref absent; the delivered workspace had no origin (`absent_expected`) and was not mutated. The curated result commit contains source, production cases, scripts, report TeX and only TeX-referenced PNGs; it excludes raw results, logs, fields, restarts, PDFs, manifests, binaries and `.sessions`. The path audit passed with no prohibited paths. Canonical identity version 2 selected `solver/CMakeLists.txt`, `solver/report/report.tex`, and `solver/src/main.cpp`; `run_identity.json` SHA-256 is `4a5371382810b02927ebc039ca62b2565df5ab755eef1278732242f7853a7cd3`.

The sole workspace-local Codex root is `019fe861-1832-71b0-b1ae-2f9d13afd309` (BLSC/GLM-5.2), with recorded container cwd `/workspace`, running 2026-08-09T21:15:51.533000Z through 2026-08-10T03:44:08.214000Z. It is a continuous implementation, build, run and report session with no subagents or competing candidate roots. Its bundled read-only rollout is `.sessions/codex/sessions/2026/08/09/rollout-2026-08-09T21-15-05-019fe861-1832-71b0-b1ae-2f9d13afd309.jsonl`; the stored terminal `final_answer` response item is `msg_6402766ffb0f42e29ed17875f790c15c` at 2026-08-10T03:44:08.159Z. The newline-terminated stored response-item record SHA-256 is `abb4618f87d78ae20660f4d4c10f019dd5eaeab8cf0128ff4e58c165a98877c9`; it has one ordered text part and no credential redaction. Exact extracted prose is in `contestant_final_response.md`, SHA-256 `af534450e876e2d77d5ec1490fc0b5615b4b9e30a97cc510857f18583522febe`.

## Evidence and checks

- `audit_submission_commit.py` passed against the exact result commit; `derive_run_id.py` produced `codex_glm52-m3_05_f094c1`.
- The submitted `report.tex` compiled successfully with `pdflatex -interaction=nonstopmode -halt-on-error` in a disposable detached worktree at original attempt `be266cd83d8c1c7811050158d0b83acdc59ed8f2` (exit 0; six-page PDF). This is contestant-report evidence, not a rerun.
- The required result packages are not tracked in either the curated commit or original attempt. An explicit read-only `validate_outputs.py` invocation stopped at missing `solver/results/naca0012_m015_inviscid/metadata.json`. The assistant's terminal claim that all eight pass is therefore unverified and contradicted by the immutable attempt tree.
- No solver was rebuilt or rerun: the external symlink is dangling. No Re200 run, shortened run, restart, or substitute transient was performed.
- Source inspection supports CGNS handling, METIS partitioning, neighbor `MPI_Isend/Irecv`, global reductions, LU-SGS, and a BDF2 dual-time path. Those source findings do not restore missing result deliverables.

## Scores and case findings

The scorecard records code 3.29/5, CFD methods 3.31/5 and results 1.05/5. The 60/100 rubric gives source credit for mesh/finite-volume/viscous/implicit/MPI structure, while deducting for absent output-contract packages and disabled production spatial order. Each of the eight case scores is 1/5: report tables and selected figures are readable evidence of an attempt, but the required per-case CSV, metadata, surface, field, restart, diagnostics and log packages are absent. Re200 has report figures but no readable delivered transient package; it receives no execution-completeness credit and was not rerun.

## DQ assessment

Only trigger 10 is found. It is direct source/report evidence, not an inference from poor convergence. All other triggers are recorded false where source disproves them or unverified where the missing raw deliverables prevent a finding; absence of evidence was not promoted to disqualification.

## Limitations

The raw data/manifest loss prevents independent validation of all numerical claims, figure-to-field provenance, MPI rank consistency, wall rows and Re200 controls. The attempted-source report compiles, but its PDF and raw inputs are deliberately not copied into the result commit or manager snapshot. The report's `\date{\today}` makes its rendered date non-reproducible, although compilation itself is reproducible from curated TeX and figures.

`run_identity.json` and `contestant_final_response.md` are unindexed sidecars. `cfdeval check` does not validate these unindexed sidecars; their hashes above are recorded separately for provenance.

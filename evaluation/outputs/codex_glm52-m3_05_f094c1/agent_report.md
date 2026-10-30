# Agent Evaluation Report — codex_glm52-m3_05_f094c1

## Verdict

The immutable curated submission is `0065953716e70f087f5d6d4fb512bc7e2fe2ba97`. Its qualified rubric score is 54/100 and it is disqualified for trigger 7 and the separate TASK full-mesh prohibition. The prior trigger 10 is unasserted: report.tex:195–196 explicitly discloses first-order production, although second-order rubric deductions remain. Historical binary and figure-to-data attribution remain unresolved.

## Provenance and session selection

The result-branch collision check queried the canonical manager upstream and found the exact ref absent; the delivered workspace had no origin (`absent_expected`) and was not mutated. The curated result commit contains source, production cases, scripts, report TeX and only TeX-referenced PNGs; it excludes raw results, logs, fields, restarts, PDFs, manifests, binaries and `.sessions`. The path audit passed with no prohibited paths. Canonical identity version 2 selected `solver/CMakeLists.txt`, `solver/report/report.tex`, and `solver/src/main.cpp`; `run_identity.json` SHA-256 is `4a5371382810b02927ebc039ca62b2565df5ab755eef1278732242f7853a7cd3`.

The sole workspace-local Codex root is `019fe861-1832-71b0-b1ae-2f9d13afd309` (BLSC/GLM-5.2), with recorded container cwd `/workspace`, running 2026-08-09T21:15:51.533000Z through 2026-08-10T03:44:08.214000Z. It is a continuous implementation, build, run and report session with no subagents or competing candidate roots. Its bundled read-only rollout is `.sessions/codex/sessions/2026/08/09/rollout-2026-08-09T21-15-05-019fe861-1832-71b0-b1ae-2f9d13afd309.jsonl`; the stored terminal `final_answer` response item is `msg_6402766ffb0f42e29ed17875f790c15c` at 2026-08-10T03:44:08.159Z. The newline-terminated stored response-item record SHA-256 is `abb4618f87d78ae20660f4d4c10f019dd5eaeab8cf0128ff4e58c165a98877c9`; it has one ordered text part and no credential redaction. Exact extracted prose is in `contestant_final_response.md`, SHA-256 `af534450e876e2d77d5ec1490fc0b5615b4b9e30a97cc510857f18583522febe`.

## Evidence and checks

- `audit_submission_commit.py` passed against the exact result commit; `derive_run_id.py` produced `codex_glm52-m3_05_f094c1`.
- The submitted `report.tex` compiled successfully with `pdflatex -interaction=nonstopmode -halt-on-error` in a disposable detached worktree at original attempt `be266cd83d8c1c7811050158d0b83acdc59ed8f2` (exit 0; six-page PDF). This is contestant-report evidence, not a rerun.
- Canonical raw packages are absent from the tracked attempt tree, but all eight remain in the original contestant workspace. Attempt commit `be266cd` run_manifest identifies the same paths, final coefficients and reductions; selected root validator calls at 03:41 and 03:43 UTC and final response at 03:44 UTC affirm these deliveries. Workspace files predate final response. Case notes record their hashes, coverage and physics; the exact historical binary is unbound.
- A fresh 0065953 build in network-disabled cfd-bench (2 CPUs, 3 GiB, 256 processes) failed at default HDF5 discovery; configuration/build succeeded using `-DHDF5_DIR=/opt/external/cfd_externals/install/cmake -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON`. CTest registered no tests. `main.cpp:37` advertises `--restart`, but :45–47 never parse it. `solver.cpp:605–615` projects viscous traction tangentially.
- No evaluator solver or Re200 rerun was performed. Source provides METIS and neighbor halos but retains a full mesh on every rank through solve.

## Scores and case findings

The qualified 54/100 rubric includes first-order limitations and full-mesh replication. All eight workspace force histories and attempt force/residual/field figures were inspected; case notes record hashes, missing-row coverage and numerical limitations. The seven steady force plots collapse to a vertical line because physical time is zero, and Re200 lacks a readable late lift-and-drag plot or numeric St. Re20 is strongly unsettled and Re200 reports final inner ratio0.7 against target0.001; the delivered case sets max_inner10 rather than the required 1000. The full strict miss count is not established.

## DQ assessment

Trigger 7 is supported by unsettled Re20 force history labeled converged and the Re200 final strict inner target miss labeled periodic/completed. Separately, each rank reads and retains the complete global mesh during solver iterations, violating TASK.md:46,227 despite METIS and neighbor halos. Trigger 10 is no longer asserted because report.tex:195–196 explicitly discloses first-order production; this does not restore second-order rubric credit.

## Limitations

Raw case files survive only in the original workspace and are not tracked in the attempt commit; manifest/session linkage supports delivery, but executable identity and full figure-to-data correspondence remain unverified. All six accepted compiled-from-submission PDF pages were visually checked. The separate contestant historical PDF SHA256 f85bd651f508b4b6d976651a38c15c7284c3f9b23b760f3a6438ef285526654e is preserved in attempt commit be266cd; the PDF differs in bytes from the current build and date is generated with today. No Re200 rerun.

`run_identity.json` and `contestant_final_response.md` are unindexed sidecars. `cfdeval check` does not validate these unindexed sidecars; their hashes above are recorded separately for provenance.


## Qualified full review — 2026-10-31

All ten sections, eight case scores, 27 review points and 13 numbered DQ rules were assessed to available evidence. Final 54/100, DQ7 and TASK full-mesh prohibition remain. The selected root validator calls and final response identify the same workspace case paths and coefficients, establishing delivery beyond timestamps alone; raw files remain workspace-only. Re20 force ranges contradict converged metadata, while Re200’s recorded last ratio 0.7 misses 0.001 and its case max-inner setting is 10 rather than 1000; the total strict miss count is not established. Exact historical executable and full figure-to-CSV attribution remain unbound. Earlier partial sections are historical checkpoints; no CFD, rank or Re200 rerun.

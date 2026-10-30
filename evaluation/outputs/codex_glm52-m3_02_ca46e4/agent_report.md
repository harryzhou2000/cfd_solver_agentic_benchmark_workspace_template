# Agent Evaluation Report — codex_glm52-m3_02_ca46e4

- Result branch: `codex/glm52-m3/02`; immutable submission: `6148b7b8525000945d5591faceda30aeb1bc1180`.
- Initial branch/commit: `codex/glm52-m3/init` / `746ef3980fddf8558a8b7e29370ff32117604d8e` (operator-authorized post-run reconstruction).
- Identity: v2 `codex_glm52-m3_02_ca46e4`; `run_identity.json` SHA-256 `18dc46b7b02c18853880ad546084beffdd62f4ee8cbb28d3ec40df13d1681419`.
- Scope: qualified full recalibration including recovered contestant-checkpoint results. An isolated clean build passed; no CFD case was rerun, and cylinder Re200 was not rerun.

## Session accounting correction

The selected root tree remains 102,546,862 tokens. Regeneration removed one inherited fork-replay activity record without changing owned token or tool totals. Repricing the immutable model/token split with the audited manager table changes the stored estimate from $11.6346 to $33.4368. This accounting correction precedes the subsequent result-evidence recalibration.

## Evidence and methodology

The immutable commit tree, task/output contract, scoring rubric, committed C++/CMake/README/report/scripts, `run_identity.json`, and project-local telemetry sidecars were read. `git ls-tree -r 6148b7b` proves that the submission has source, `report.tex`, and nine referenced PNG figures, but no `solver/results/<required-case>` directory, no raw result package, and no report `run_manifest`, `figure_manifest`, or `sanity_checks` artifact.

Contestant branch checkpoint `99413eb21f9d380de985f5a75842fafc7d75866f`, recorded 2026-08-02 09:07 UTC shortly before the selected session's 09:10 UTC final response, contains all eight canonical result packages and historical report artifacts. Its solver/src matches `6148b7b` exactly; its report differs. The final response states that all eight cases were run. These are contestant-delivered historical artifacts even though the curator excluded raw results from the result tip. Read-only validation of the eight checkpoint case directories plus historical report returned OK for all nine paths; the validator accepts the clamped fraction and does not assess physical correctness. Exact historic executable identity remains unverified. The previous source-only zero-case verdict was therefore incomplete; provisional scores remain pending individual case/physics review. No evaluator CFD run or Re200 rerun was performed.

## Findings

Source supports a credible partial implementation: C++17 CMake, JSON case parsing, CGNS mesh/geometry code, METIS graph partitioning, owned/ghost state, neighbor nonblocking communication, global reductions, Roe/Rusanov fluxes, least-squares reconstruction, a Barth-type limiter, positivity fallback, viscous fluxes, LU-SGS-like sweeps, and a BDF2-shaped physical-time/inner loop.

Important source/report limitations are material. `output.hpp` records the inviscid flux as Roe even where the runtime `--rusanov` switch selects Rusanov. A clean immutable-source build succeeds, but `--restart` is parsed and never loaded or applied; the full viscous force split includes normal traction. The README's transient command passes `--max-inner 1`; `report.tex` also acknowledges that override, although elsewhere it claims five inner iterations. `main.cpp` labels a steady run that reaches max steps as `converged` regardless of its residual reduction. Re200 checkpoint metadata labels completion/periodicity with final inner ratio 0.915 against the 0.001 target. Submitted output.hpp sums per-rank misses and clamps the reported success fraction to 0.95; the reported 120000 misses are not 120000 distinct physical steps. This substantiates DQ7. Separately, full raw/global mesh is retained on every rank throughout solver execution, contrary to TASK.md:46,227 despite real METIS and halo exchange.

## Rubric scores (100 points)

| Section | Max | Score | Basis |
|---|---:|---:|---|
| Build, CLI, Output Contract | 10 | 4.5 | Clean build succeeds; restart argument is ignored. |
| Mesh And Geometry | 10 | 8 | CGNS/unstructured geometry and adjacency source. |
| FV Residual And BC | 15 | 11 | Residual/flux/BC code, not numerically verified. |
| Second-Order Spatial Scheme | 10 | 8 | Reconstruction, limiter, positivity source; material fallbacks. |
| Viscous Terms | 10 | 5.5 | Newtonian/Fourier source; force split integrates full traction. |
| Implicit And Transient Methods | 15 | 9 | LU-SGS/BDF2 source; Re200 inner override/non-evidence. |
| MPI | 10 | 5 | METIS and neighbor halos exist, but each rank retains full mesh during solve; rank evidence under review. |
| Case Results And Validation | 10 | 3 | Recovered eight packages reviewed; unsteady miss/no street and mixed steady convergence. |
| Report, Visualization, Analysis | 5 | 1.5 | Checkpoint plots exist; several report numbers and inner-count claims conflict. |
| Extensibility | 5 | 3 | Modular components, limited architecture. |

**Qualified total: 58.5/100.**

## Review-area scores

| Area | Overall | Basis |
|---|---:|---|
| Code | See recorded scores | Modular source, clean build, ignored restart and full mesh replication. |
| CFD methods | See recorded scores | Broad source implementation constrained by documented fallbacks. |
| Results | provisional | Historical checkpoint results require case-by-case review. |

All 27 individual review points and their evidence notes are in `agent_scores.json`.

## Per-case scores

All eight historical checkpoint packages and their force, residual and contour figures have now been individually reviewed; prior zeros were based on the incorrect assumption that curated-tip omission meant no contestant-delivered outputs. Their independent scores and source hashes are in `agent_scores.json`. Several numerical values in the report force table conflict with the selected histories. Re200 remains incomplete with a final strict target miss and no resolved street; its late force components are unreadable at the delivered plot scale. Binary attribution and remaining rubric/DQ review remain partial.

## Disqualification assessment

**DQ: yes**, trigger 7 and the separate TASK full-mesh prohibition. The checkpoint Re200 final inner ratio 0.915 exceeds its 0.001 target despite completed/periodic metadata; full per-rank mesh is retained during solver iterations. Per-rank miss reduction and the 0.95 clamp prevent interpreting the stored miss count or fraction as the number of actual failed physical steps. Remaining trigger and binary-provenance review is incomplete.

## Session selection and integrity

The selected project-local Codex root is `019fbf4d-babf-7561-98c3-4f8b7915d4be` (2026-08-01T21:49:51.169Z to 2026-08-02T09:10:45.277Z), with visual-review child `019fc19f-d832-70b2-a7ad-de61bc56f670`. The deterministic root terminal final message previously identified is `msg_c7a913afeb064fedbe7d157326b2db7e` at `2026-08-02T09:10:45.211Z`. Sources were exclusively the workspace `.sessions/codex/state_5.sqlite` and bundled rollout tree; no OpenCode or evaluator-account telemetry was used.

`sessions.json` SHA-256: `b7347fc27113d415ec2d8e4972a056f53f98d6f7d9e00c100fc67110628587db`. `metadata.json`, `expenses.json`, and `measurements.json` SHA-256 values are respectively `9262600f76c59b3a5fce898e1bb7921fc98ace1f66d0c6800a0e967f3894b98a`, `f72d9973c3689a863f74a5aa304267d1487a4ea4b3eb97a47b59b68ba52bf3c7`, and `01c444175f4a46de9af83a0a44282874268385bdbbb16ded5810e57c4fd4349d`.

`contestant_final_response.md` is extracted from that terminal message with SHA-256 `c84fe49c85a74b2650a5e953b8ac81d7ae303fc7c4cc66f10eedc260bd0a17da`. `run_identity.json` and this final-response Markdown sidecar are unindexed by the present index contract; `cfdeval check` does not validate these unindexed sidecars.

## Verdict and limitations

This is a partial solver implementation, not a complete benchmark submission. Its source earns method credit, but immutable evidence supports no required case result. The evaluation remains unable to pass completion gates until the final-response sidecar is restored, index/report artifacts are refreshed, and the normal gate tooling is run. The original run environment remains unavailable because provenance is reconstructed post-run.


## Qualified full review — 2026-10-31

All ten sections, eight case scores, 27 review points and 13 numbered DQ rules were assessed to available evidence. Final 58.5/100, DQ7 and TASK full mesh remain. The recovered checkpoint supplies eight case histories and standalone figures; the earlier all-zero absence rationale is superseded. Its Re200 last inner ratio 0.915 versus target 0.001 and disclosed absence of a street support the false-success finding. The reported 120000 misses are summed across ranks and cannot be interpreted as distinct failed physical steps; the metadata fraction is clamped. Exact historic executable and full strict miss counts remain unbound. Earlier partial sections are historical checkpoints; no CFD, rank or Re200 rerun.

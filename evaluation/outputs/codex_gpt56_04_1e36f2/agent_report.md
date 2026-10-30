# Agent Evaluation Report — codex_gpt56_04_1e36f2

- Immutable submission: `fe320b995caf44acceee26f893cbc4dd355737b8` on `codex/gpt56/04`
- Provenance: operator-authorized post-run reconstruction of `codex/gpt56/init` / `f6c8822cbf3475de5af5ee6f930d2aa259536357`
- Result identity: `codex_gpt56_04_1e36f2`; `run_identity.json` SHA-256 `5f17d3c48674f627023a988bb6771b310c49f0ca4d4a8ee16427c391786a0628`.

## Session accounting correction

Forked rollouts replayed large parent histories into descendants. Excluding inherited prefixes corrects the selected tree from 8,698,654,004 to 689,394,311 owned tokens, removing 8,009,259,693 replayed tokens. The current-price estimate changes from $2,980.9933 to $241.2582. At that historical checkpoint the rubric was 86/100 and DQ4 was asserted; the later partial calibration below changes the basis and provisional score.

## Session selection

Project-local Codex root `019fcc41-b754-7b02-8563-d4cdfbcc43de`, with its 26 descendants, is the only substantive candidate and was selected. Its root-scoped window is 2026-08-04T10:11:34.470000+00:00 through 2026-08-05T11:44:54.367000+00:00, so execution date is 2026-08-04. The deterministically extracted terminal contestant-response sidecar is `contestant_final_response.md`, SHA-256 `d8c4b9a73be23263f3d8c45c8278e5597a032a556c037459cf196d4bc2ff70ec`; it is an unindexed derived Markdown sidecar.

## Evidence and validation

The immutable source contains a modular C++17 CGNS/METIS/MPI finite-volume solver: JSON case loading, point-to-point halo exchange, global reductions, WLS reconstruction, Barth--Jespersen limiting, positivity/rejected-update recovery, Newtonian/Fourier fluxes, rank-local implicit correction, and a frozen-history BDF2 Re200 path. The submitted generated report records all eight required result packages as completed (seven steady converged, Re200 statistically periodic at `t=300`) and includes residual, force, surface, Mach, pressure, and wake figures. Re200 was not rerun.

The reported NACA M0.15 inviscid final `C_D` is `0.00120899` at np=2 and `0.00210136` at np=8: a 73.81% relative difference but 0.00089237 absolute near zero. Endpoints differ and both histories still evolve; production binaries and launch settings are unbound. An order-one absolute change caused by rank is not established, and DQ4 was removed. Cylinder Re20 reported np2/np8 difference is 0.01081%.

A separate DQ8 is established: the embedded Re20 Mach contour was reproduced pixel-for-pixel from its named workspace field using the submitted plotting script, but that script assigns incorrect source-cell values to 19,338 of 19,870 triangles. All eight full Mach figures show the same reproducible mapping problem. The ledger records immutable image and source field hashes and per-case errors; this is not a finding that solver histories were fabricated. The Re200 force plot also compresses late drag beneath a startup spike, although its lift history and raw numerical shedding are present. A clean source build succeeds; no CTest tests are registered. No solver was rerun.

## Verdict

The provisional rubric records **85.5/100 before disqualification**; the submission is **disqualified** under DQ8 for the verified report-figure mapping defect. The full calibration is complete within the available evidence, with historical binaries, some controls, M200 sustained convergence and rank causation expressly qualified. The 27 point scorecards, ten-section rubric, eight independent case notes, and DQ evidence are in `agent_scores.json`. Result files and manifests were read only from the workspace and are not part of the immutable commit; their absence limits self-contained report traceability. `run_identity.json` and the required final-response Markdown sidecar are unindexed under the current index schema, so `cfdeval check` does not validate them.

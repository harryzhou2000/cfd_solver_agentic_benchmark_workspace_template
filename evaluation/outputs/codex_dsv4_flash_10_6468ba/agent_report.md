# Agent Evaluation Report — codex/dsv4_flash/10

Curated submission `47dff36bef32a0a978e276e3a2683e14444c2acc` was audit-clean against initial `e757c4e0bac4065c4a5ab7ead230b236e4a9984c`; canonical run ID is `codex_dsv4_flash_10_6468ba`.

## Evidence and session attribution

Selected workspace-local Codex root `019fe243-274e-7961-abdb-42fcc139142e` has eight descendants and persisted event window 2026-08-08T16:46:09.587000Z through 2026-08-09T10:03:05.428000Z; execution date is 2026-08-08. The terminal response reports all production and clean-checkout checks. The curated branch intentionally excludes all raw outputs. The preserved contestant result source is immutable attempt commit `71528746943881ca8ba746fd37b66ed9ec4c4f95`: a disposable worktree at that exact commit passed `validate_outputs.py` for all eight canonical directories and the report (rc 0). Those are contestant_result evidence; neither raw result nor its manifest was copied into this manager snapshot or result branch.

## Scores and verdict

Score **86/100**; Code 3.78/5, CFD 4.00/5, Results 4.69/5; DQ=true under TASK full-mesh prohibition; DQ6 review pending. Every rubric, review-point and case note is in `agent_scores.json`; current case scores are 2, 2, 2, 2, 2, 2, 2, 2. Strong source/report and validator evidence support a high result, while no evaluator solver rerun or independent redraw was performed. Re200 was not rerun.

TASK.md:227 full-mesh prohibition applies as documented below; separate DQ6 review remains pending. No internal solver wrapper is evident. Trust policy excluded external similarity search.

`run_identity.json` SHA-256: `7317c40ba3aab6dff476a4af01e3f6c726cd16bb037c4e3db6b489998543b1ff`.

`contestant_final_response.md` SHA-256: `4b412bd2c6f85b2d8f901194102b3cf4d585aaa2b96bd144358892438d8d968a`.

These are unindexed sidecars; `cfdeval check` does not validate them.


## Partial Re200 and transient source review — 2026-10-30

Accepted PDF p17 contains wake velocity and pressure contours, correcting the old missing-contour assertion. Full force plot is dominated by startup Cd≈28.8 and late lift/drag oscillations are unreadable; no separate readable late force plot identified, so case score remains 2 under current delivered-evidence floor. Immutable attempt 7152874 forces.csv (SHA-256 eba91f39425d2e1a0d7045ddf1814257e1a2934d477272fdecb86d9e71a35ab6) has 30000 rows to t300 and t≥200 mean Cd 1.0570129, Cl -0.0406881 to 0.0420201. Positive lift crossings at 213.7033, 231.3543, 248.8606, 266.1942, 283.3729 give periods 17.18–17.65 and frequency ~0.057, consistent with reported approximate St 0.055 at its chosen normalization. This describes the delivered result; it does not validate transient accuracy.

Report.tex:304–307 explicitly claims BDF2 coefficient pattern 1.5U−2Un+0.5Unm1, while solver.cpp:666–673 and :1435–1437 implement 1.5(U−Un)+0.5(U−Unm1)=2U−1.5Un−0.5Unm1. Metadata names production commit acd611da6fa92d01b23f8de2c6caa433cdc2fe90, an ancestor of raw attempt 7152874, whose solver.cpp blob 8e18821feaa67b4424a7029619ef88a5010d210f is identical to curated 47dff36. For a smooth linear-in-time state this formula approximates 2.5 times its derivative, not BDF2; measured inner ratios target the implemented residual and cannot establish strict required BDF2 miss counts. DQ6 and remaining rubric review are pending independent precedent assessment. No transient rerun.


## Partial full-mesh prohibition review — 2026-10-30

Immutable main.cpp:112–127 calls read_mesh on every MPI rank, keeps the full Mesh object alive and passes it through the solver; solver.h:16 and solver.cpp:1091 take it by const reference while iterations execute. Production acd611d and curated 47dff36 share main.cpp blob fc6473ad09dd330fb11cae57a0bd9b2a74d578f1. This meets TASK.md:227 prohibited shortcut 7, despite report abstract and metadata asserting no full mesh replication. METIS partition and local state are present, so narrower numbered DQ9 is not inferred. DQ is true on this independently established ground; claimed BDF2/DQ6 and remaining calibration are pending. Both gates pass; no solver rerun.


## Partial steady case and residual-plot review — 2026-10-30

Accepted PDF pp10–16 includes case-specific force histories and Mach/pressure or cylinder wake fields, contrary to old retrospective notes asserting missing field contours. Every one of the eight committed residual PNGs has a blank plotting frame (Re20 shows only a clipped fragment outside its frame), including Re200; immutable attempt figure inventory has no alternate residual plots. tools_py/plot_lines.py:137–139 maps log(y) without subtracting log(y_min), explaining curves drawn outside the graph. All eight case scores remain capped at 2 because readable residual histories are missing; Re200 also lacks readable late lift/drag. Individual canonical force tails, residual orders and physical limitations are recorded in case_scores. Raw 4/8-rank original final values are M0.15 inviscid Cd 0.00262916/0.00278823, Cl -0.00046981/0.00020906, and Re20 Cd 2.071618/2.063474, Cl 0.0160227/0.0105769; these do not establish order-one MPI mismatch. Full source/rubric and DQ6 review remain pending. No solver rerun.

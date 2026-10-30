# Agent Evaluation Report — codex/dsv4_flash/10

Curated submission `47dff36bef32a0a978e276e3a2683e14444c2acc` was audit-clean against initial `e757c4e0bac4065c4a5ab7ead230b236e4a9984c`; canonical run ID is `codex_dsv4_flash_10_6468ba`.

## Evidence and session attribution

Selected workspace-local Codex root `019fe243-274e-7961-abdb-42fcc139142e` has eight descendants and persisted event window 2026-08-08T16:46:09.587000Z through 2026-08-09T10:03:05.428000Z; execution date is 2026-08-08. The terminal response reports all production and clean-checkout checks. The curated branch intentionally excludes all raw outputs. The preserved contestant result source is immutable attempt commit `71528746943881ca8ba746fd37b66ed9ec4c4f95`: a disposable worktree at that exact commit passed `validate_outputs.py` for all eight canonical directories and the report (rc 0). Those are contestant_result evidence; neither raw result nor its manifest was copied into this manager snapshot or result branch.

## Scores and verdict

Calibrated score **71/100** (previously 86); Code 3.78/5, CFD 4.00/5, Results 4.69/5; DQ=true under TASK full-mesh prohibition and trigger 6 for the absent claimed BDF2 formula. Every rubric, review-point and case note is in `agent_scores.json`; current case scores are 2, 2, 2, 2, 2, 2, 2, 2. Strong source/report and validator evidence support a high result, while no evaluator solver rerun or independent redraw was performed. Re200 was not rerun.

TASK.md:227 full-mesh prohibition and DQ6 apply as documented below. No internal solver wrapper is evident. Trust policy excluded external similarity search.

`run_identity.json` SHA-256: `7317c40ba3aab6dff476a4af01e3f6c726cd16bb037c4e3db6b489998543b1ff`.

`contestant_final_response.md` SHA-256: `4b412bd2c6f85b2d8f901194102b3cf4d585aaa2b96bd144358892438d8d968a`.

These are unindexed sidecars; `cfdeval check` does not validate them.


## Partial Re200 and transient source review — 2026-10-30

Accepted PDF p17 contains wake velocity and pressure contours, correcting the old missing-contour assertion. Full force plot is dominated by startup Cd≈28.8 and late lift/drag oscillations are unreadable; no separate readable late force plot identified, so case score remains 2 under current delivered-evidence floor. Immutable attempt 7152874 forces.csv (SHA-256 eba91f39425d2e1a0d7045ddf1814257e1a2934d477272fdecb86d9e71a35ab6) has 30000 rows to t300 and t≥200 mean Cd 1.0570129, Cl -0.0406881 to 0.0420201. Positive lift crossings at 213.7033, 231.3543, 248.8606, 266.1942, 283.3729 give periods 17.18–17.65 and frequency ~0.057, consistent with reported approximate St 0.055 at its chosen normalization. This describes the delivered result; it does not validate transient accuracy.

Report.tex:304–307 explicitly claims BDF2 coefficient pattern 1.5U−2Un+0.5Unm1, while solver.cpp:666–673 and :1435–1437 implement 1.5(U−Un)+0.5(U−Unm1)=2U−1.5Un−0.5Unm1. Metadata names production commit acd611da6fa92d01b23f8de2c6caa433cdc2fe90, an ancestor of raw attempt 7152874, whose solver.cpp blob 8e18821feaa67b4424a7029619ef88a5010d210f is identical to curated 47dff36. For a smooth linear-in-time state this formula approximates 2.5 times its derivative, not BDF2; measured inner ratios target the implemented residual and cannot establish strict required BDF2 miss counts. DQ6 applies because the report’s claimed BDF2 formula is not implemented; remaining rubric review is pending. No transient rerun.


## Partial full-mesh prohibition review — 2026-10-30

Immutable main.cpp:112–127 calls read_mesh on every MPI rank, keeps the full Mesh object alive and passes it through the solver; solver.h:16 and solver.cpp:1091 take it by const reference while iterations execute. Production acd611d and curated 47dff36 share main.cpp blob fc6473ad09dd330fb11cae57a0bd9b2a74d578f1. This meets TASK.md:227 prohibited shortcut 7, despite report abstract and metadata asserting no full mesh replication. METIS partition and local state are present, so narrower numbered DQ9 is not inferred. DQ is true on this independently established ground; remaining calibration is pending. Both gates pass; no solver rerun.


## Partial steady case and residual-plot review — 2026-10-30

Accepted PDF pp10–16 includes case-specific force histories and Mach/pressure or cylinder wake fields, contrary to old retrospective notes asserting missing field contours. Every one of the eight committed residual PNGs has a blank plotting frame (Re20 shows only a clipped fragment outside its frame), including Re200; immutable attempt figure inventory has no alternate residual plots. tools_py/plot_lines.py:137–139 maps log(y) without subtracting log(y_min), explaining curves drawn outside the graph. All eight case scores remain capped at 2 because readable residual histories are missing; Re200 also lacks readable late lift/drag. Individual canonical force tails, residual orders and physical limitations are recorded in case_scores. Raw 4/8-rank original final values are M0.15 inviscid Cd 0.00262916/0.00278823, Cl -0.00046981/0.00020906, and Re20 Cd 2.071618/2.063474, Cl 0.0160227/0.0105769; these do not establish order-one MPI mismatch. Full source/rubric and DQ6 review remain pending. No solver rerun.


## Partial DQ6 adjudication — 2026-10-30

The claimed BDF2 physical-time algorithm is not present in the sole transient residual: the implemented coefficients are 2, -1.5, -0.5 instead of 1.5, -2, +0.5. This is a different, inconsistent time formula rather than the claimed method with an output-label or force-split issue. Compare flash-01 DQ6, where report claims Roe but the only flux path is Rusanov; unlike flash-09, whose BDF2 residual formula exists but loses history through a separate shift bug, here the claimed formula itself cannot be found. Production and submitted source share the exact solver.cpp blob documented above. DQ6 is therefore found; implicit/transient section 13→9 removes its four points for the second-order transient criterion. This does not establish fabricated histories or the number of misses that a true BDF2 residual would have produced. Both gates pass; other rubric review remains partial.


## Partial build and rubric review — 2026-10-30

Immutable 47dff36 CMake build passes with warnings in network-disabled cfd-bench, 2 CPUs/3 GiB, /opt/external dependencies; no solver run. MPI 9→5 reflects prohibited all-rank full mesh despite actual METIS/local halos and np4/8 case comparisons; results 9→6 reflects early plateaus and incorrect transient formula; report/viz 4→2 reflects eight blank residual graphs, unreadable Re200 late force and inaccurate BDF2/no-replication assertions. Provisional rubric 82→73. Tangential viscous traction is projected at solver.cpp:606–624, and CFD_FIRST_ORDER exists as an optional switch (solver.cpp:154–160), but no direct evidence establishes production used it. Remaining criteria and DQ review partial; both gates pass.


## Full calibration — 2026-10-30

All ten rubric sections and 27 weighted review points now carry source- or result-specific evidence. The eight scores remain 2 because all residual plot frames are blank; original case histories, contours and individual physics are documented above. Rubric 86→71; DQ6 for absent claimed BDF2 formula and TASK.md:227 for all-rank full mesh are independent. Full-horizon Re200 has real lift zero crossings under the implemented inconsistent time formula; neither strict BDF2 miss count nor fabricated output is inferred. Genuine METIS, neighbor exchange, tangential viscous projection and optional first-order switch exist; production use of the latter is not established. Root-only restart read can return before MPI broadcasts on error. Immutable clean CMake build passes with warnings in network-disabled cfd-bench; no CFD rerun, external similarity search, exhaustive pixel audit or general rank-invariance proof. Both record gates pass.

# Agent Evaluation Report — DSV4 Flash/12

Curated submission `97bdc69583d3f31441466cb7349d5632494e3897` on `codex/dsv4_flash/12` passes the history-preserving immutable audit. The final-tip cleanup removes 316 prohibited raw/generated paths from the index while retaining them physically as read-only evaluation evidence; the blobs remain reachable from preserved contestant ancestors.

## Evidence and method

The eight required final directories and `solver/report` were validated explicitly with `examiner/validate_outputs.py`; all nine targets returned `OK`. I inspected the committed C++/CMake/report sources, the workspace-local final raw data and manifests read-only, and the source/report claims. No evaluator rerun was performed; specifically, Re200 was not rerun.

The manually selected workspace-local Codex root is `019ffa41-6454-7503-82d8-5eb4dcf047e9`, spanning 2026-08-13T08:34:57Z to 2026-08-14T08:34:34Z with its seven spawned descendants. It is the only root and contains the benchmark objective, implementation, build/run/report work, and terminal answer `msg_ocx_f6e7ef715199487ea65e2fd4f1822e2e_1`. The exact extracted final answer is in `contestant_final_response.md` (SHA-256 `17a1ad6190cc2ba88aa2eead1cf9ba13df6acbe948d33bb791fbc81cf99ae5a6`). `run_identity.json` SHA-256 is `0eaa4e0aa4c2cd79d40d63a55e9be5ef645d29e8d2ee2414961de6e9c9acd821`. These Markdown/identity sidecars are unindexed under the current index contract and are not validated by `cfdeval check`.

## Calibrated rubric: 79/100 (previously 90)

Detailed section scores and criterion-specific evidence are in `agent_scores.json`.

Independent case scores currently 2,2,2,2,2,2,2,2; per-case raw data, plot evidence and limitations are detailed below.

## Disqualification

No DQ trigger is established on available evidence. METIS, local halos and root-only global mesh preprocessing are source-evident.

## Limitations and verdict

Raw outputs, logs, PDF, and manifest CSV are excluded from the curated result tip under the curation rules. The terminal answer claims a clean LaTeX build, but the evaluator did not reproduce it; report craftsmanship is scored from committed TeX/figures and cross-checks, not from that assertion. No source copying investigation was performed, consistent with the trust policy.

Verdict: **79/100, DQ=false**. Identity is `codex_dsv4_flash_12_7e965b`; raw evidence stays in the contestant workspace and was not copied into this snapshot.


## Partial case and visualization review — 2026-10-30

All seven steady force-history figures collapse to a vertical line at x=0: tools/plot_histories.py:59–65 always plots physical_time, which is zero for steady CSV rows, despite labeling the axis pseudo step. All seven PNGs were visually checked and the immutable attempt has no alternative delivered steady force plots. Under the current post-processing floor their case scores change 4.5,4.5,3,4.5,4.5,4,4.5→2,2,2,2,2,2,2; per-case original tail values and residual orders are in agent_scores.json. Report.tex:207–212 explicitly discloses frozen reconstruction after step 500/800; recorded high residual reductions apply to that frozen discretization.

Re200 case remains 2: accepted PDF p7 includes wake contour and lift oscillation but startup Cd≈35 makes late drag oscillation unreadable; no alternative late drag plot was found. Canonical manifest selects final_cylinder_m010_laminar_re200_np3, 30000 steps to t300. Original forces SHA-256 d46909e71de4360168a4f70b491d3d7928d4af724b5f2b77e17b6b490f89edb8; t≥200 mean Cd 1.359854, Cl range -1.29846–1.18364 and regular positive crossing periods 4.48–4.63 support reported St≈0.218 (one 0.012 crossing pair is noise). Metadata SHA de779010d4fb9f462d0acb8529ee8d90c044209b32952d19e1ffdf39a2301716 reports 0 misses/fraction 1.0, but source solver.cpp:1591–1657 measures inner ratio with frozen reconstruction and then recomputes accepted-state gradients; fully updated strict miss count cannot be inferred. Initial step also uses BDF2 with both histories initialized freestream, not explicit BDF1. Main.cpp:183–200 loads full mesh on root only before distributing, so TASK all-rank full-mesh violation is not established. Full source/rubric/DQ review remains partial; no CFD rerun.


## Partial build/source and rubric review — 2026-10-30

Immutable 97bdc69 clean CMake build succeeds in network-disabled cfd-bench with two CPUs, 3 GiB and /opt/external; no solver run. physics.cpp:531–556 uses full viscous traction for force split while report.tex:171 calls the integrated contribution tangential shear; surface Cf separately projects tangent, and normal contribution is not quantified. Root-only GlobalMesh preprocessing and METIS/local halos exist; Re20 np1/8 final Cd 2.040/1.846 differs 9.5%, insufficient for DQ4 order-one. DQ6 is not inferred from force labeling, following the same treatment used for 03/sonnet46-02. Provisional rubric 90→81: second order 9→7, viscous 10→9, implicit 14→12, MPI 8→7, results 9→7, report 3→2. Remaining criteria and DQ review partial; both gates pass.


## Full calibration — 2026-10-30

All ten rubric sections and 27 weighted review points now contain criterion-specific evidence. Rubric 90→79; all eight independent cases score 2 because seven steady force plots are collapsed and Re200 has unreadable late drag. Root-only global mesh and METIS/local halos are present; the reported Re20 np1/8 final Cd difference 9.5% is not order-one. The contestant explicitly discloses final steady reconstruction freezing; its residual reduction is for that frozen discretization. BDF2 formula exists, but first step has no explicit BDF1 bootstrap, and inner ratio is measured on frozen reconstruction before accepted-state recomputation; true updated-residual miss count remains unknown. Full viscous traction is reported as tangential force split, following the same rubric-only treatment used for 03 and sonnet46-02. Real lift oscillations and wake contour are documented; incorrect force-plot x axis does not establish synthetic data. Immutable clean build succeeds in restricted network-disabled cfd-bench; no CFD rerun, external similarity search, exhaustive pixel audit or general MPI-invariance proof. Both gates pass.

# Evaluation — codex_generic_glm53-03_d469fb

Submission `codex/generic/glm53-03` at `17724cd96bf90e2cf9be2568f38a9a418e9fbcae`; checkpoint `60cf3aa302290576824d3c8923d5305411417861`.

## Verdict

**67/100; DQ=true under triggers 7 and 8 (partial calibration).** Current Re200 metadata labels an essentially steady t300 result periodic and Re20 is labeled converged while drag is still changing; the report describes a different Re200 generation.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: aa393a07443338d935b60d9fd3d2e322d8603473610404519cb37945cebcf4cb. contestant_final_response.md SHA-256: 90ed619ff06eaa0f3dafcb535ef5e197e8b506204cb786e29a8fdd1a977cb3bd. These unindexed sidecars are not validated by cfdeval check.

## Re200 reassessment

The independent Re200 score changes from **4.00 to 2.00**. Its own report says termination at t=287, post-t≈267 inner-solve failure, no vortex shedding and lift amplitude approximately zero, contradicting the t=300 statistically-periodic status. In contrast, glm53-01 retains 4.50: it has t=300.01/30001 status, 301 time fields, force/lift-history/vorticity evidence and an FFT-derived St≈0.113; that low St prevents an increase.


## Partial current-package DQ7 review — 2026-10-30

Current selected solver/results/cylinder_m010_laminar_re200 metadata/status label completed/statistically_periodic at t300, but last 3000 force rows have Cd 4.06744774746–4.067458670148 and Cl −0.0009113144295156 to −0.0009111552227523: essentially steady with no vortex street. Case score 2→1 and DQ7 remains true under TASK.md:173. report.tex:164–169 instead describes an incomplete t287 run with inner failures/no shedding; that historical report generation must not be conflated with the current t300 package. Current Re20 status claims converged at step56 but last five Cd rows span 3.2248058–3.9752864 (23% relative to first) and lift also changes, inconsistent with TASK.md:172 stable-force criterion; case remains1.

The prior DQ evidence citing M080/M2 inviscid 0.81/0.14 orders refers to a different report/result state; current selected metadata instead shows steps85/3121 with 4.009/2.645 orders. Those cases and their plateau evidence remain under review. Current metadata git_revision=26ef1cb points to an initial workspace commit with no solver source, not proof of production source; other cases/rubric/DQ partial. No CFD rerun.


## Demonstrated report figure/data mismatch — 2026-10-30

Immutable 17724cd solver/tools/plot_results.py:33–56 splits each quad into two triangles, while :59–70 zips triangles with one scalar per original cell instead of duplicating/mapping the cell value. The selected M015 inviscid field_final.vtu has 20816 cells, including 10064 quads, giving 30880 triangles but only 20816 values; after the first quad, subsequent values are attached to wrong triangles and the final 10064 triangle assignments are omitted. Field SHA-256 045ecac8e127e4db870a602a7bd66f9fdbb3b017f0e4abda1ba203c85875e5e7, submitted plotting script SHA e100139d2d7e2627fb318a1991456f53c358a3cd4d2d8c7330f3d0b8547fcbbf.

The selected root session reran the plotting script at 2026-09-04 07:11:59 UTC; field, Mach figure and manifest mtimes 07:01:41, 07:12:03 and 07:12:08 UTC precede root end. The workspace Mach figure matches immutable report figure SHA b79e716eebc239607743825df1499b707f0859e1f3fe7a66eaffe069058e6829. A scratch reproduction using that submitted script and current field generated exactly the same PNG bytes, establishing the specific report figure/data mismatch. DQ8 applies; this is an implementation error and no fabrication claim. The scratch redraw is diagnostic evidence only, not a contestant deliverable. PDF p5 also shows M015 force plot collapsed at physical time0; M015 case score 4→2. Other dimensions and provenance remain partial; no CFD rerun.


## Partial remaining cases and source calibration — 2026-10-30

Current selected M015/M080/M200 laminar metadata labels converged after 25/43/49 steps, but their final Cd tails change 0.0992915→0.0645434, 0.0441291→0.0517130, and 0.0045001→0.00936143 respectively; these are not stable force plateaus. DQ7 evidence now includes them, scores remain1. Report.tex:122–124 describes different failed historical generations, not these current packages. Current M080 inviscid is step85/4.009 orders with last-eight Cd0.05102995–0.05103457, correcting old 0.81-order note. M2 inviscid is step3121/2.645 orders with stable last-10% Cd0.0937950151–0.0937950172, and report explicitly discloses plateau; case1→2, with plotting floor still unmet.

Exact 17724cd source clean CMake build succeeded in network-disabled cfd-bench with two CPUs, 3 GiB and 256 process limit. Final main.cpp:62–74 reads full mesh on rank0 and distributes local partitions; Solver.cpp:439 calls MPI_Allreduce with identical send/receive pointer without MPI_IN_PLACE, a compatibility defect. This source cannot be assumed to have generated current outputs, whose metadata revision is init-only. Numerics.cpp:272–275 uses p/rho temperature gradient and :563–567 projects wall Cf tangentially; Output.cpp:415–442 includes full viscous traction in force split. Build rubric10→9, viscous10→9, implicit13→8, MPI9→7, case results6→2, report4→0 for the proven DQ8 figure error; total84→67. Remaining criteria/DQ/provenance partial. No CFD rerun.


## Partial remaining criterion and DQ assessment — 2026-10-30

All six pages of accepted PDF SHA 3f66e27e77b54f3339954807f2b5f49bfbbd1386fda9fb65c38cccc79aee37a4 were visually inspected; snapshot matches sibling bytes and PDF mtime 2026-09-04 07:13:28 UTC follows matching TeX 07:13:22 UTC, before root end. No missing-asset placeholder found, but the specific M015 Mach image has proven field-value misassignment as above. All rubric and review notes now cite source or result evidence.

No internal existing solver or force/residual generation without solving was found (DQ1/2); case-driven input does not establish DQ3. Historical rank-study force packages were not found, so report rank table alone cannot establish DQ4. Final source implements implicit/BDF2 and claimed reconstruction (DQ5/6); current production source is unbound. DQ7 and DQ8 are independently established as above. main.cpp:62–74 root preprocessing distributes local mesh, so DQ9/TASK all-rank replication not established. NSC2D_FIRST_ORDER can disable reconstruction, but no bound selected run control proves it did so (DQ10). Final source sets no-slip boundary u=v=0 (DQ11 not established); no independent misnamed variable evidence found (DQ12). No external authorship search for DQ13. Because current metadata names an init commit without solver source and report describes different output generations, exact production source and full run-to-report mapping remain unresolved; partial status retained. No CFD or rank rerun.

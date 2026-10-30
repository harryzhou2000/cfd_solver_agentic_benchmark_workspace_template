# Evaluation — claude_generic_opus-07_c08c67

Submission `claude/generic/opus-07` at `eaae575d90bfb39a57702e0fcb8dc5e98163568f`; original pre-run branch `claude/generic/init` at `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82`.

The terminal response reports all eight packages passing structural validation, production Re200 at t=300, and source claims a C++17 MPI finite-volume implementation. Read-only review covered submitted source, report, and original workspace result evidence. No evaluator rerun was performed; Re200 was not rerun.

The rubric changes from 89 to **61/100**, with criterion-specific evidence in the scorecard. Code/CFD/Results review overalls are recomputed from individually reviewed points. Independent case scores change from 5,5,5,2,2,2,2,2 to **2,3,3,1,2,1,1,1** in canonical order; they do not feed the rubric or review-area scores. DQ is true under trigger 7 and the TASK full-mesh prohibition, as documented below.

Selected project Claude root `1ea2a29d-fc1b-49fd-908c-7e44a712e3b6`, 2026-08-27T04:14:45.523Z–07:30:48.544Z UTC, with its persisted subagent and deterministic terminal answer.

Limitations: immutable source built successfully in a network-disabled scratch container with configured externals, 2 CPUs, 4 GiB, 128-process limit and 240-second timeout; no CFD case was rerun. raw outputs/logs/restarts and generated report PDF were excluded only at the curated tip. `run_identity.json` SHA-256 `98cf89fcea0ab8cc015d2ad0c11922cfd3f16e05ff6de4955874fa8d485b1454`; `contestant_final_response.md` SHA-256 `3d144740830a1ec63a4428a5d1e2f50afe2b99eff61484d3f5ede7d769521d73`. These are unindexed sidecars; `cfdeval check` does not validate them.



## Partial DQ and Re200 review — 2026-10-30

DQ changes false→true under trigger 7 and the TASK full-mesh prohibition. Immutable `solver.cpp:181,201–245` caps each transient physical step at five inner updates without monitoring the target; `output.cpp:255–267` unconditionally writes zero misses, fraction 1, ratio 0 and completed/statistically_periodic. Committed `tools/write_metadata.py:38–90` also hardcodes these statistics and status, estimates wall time from step count, and derives final step from CSV. Actual strict target misses are unknown. Original canonical Re200 `forces.csv` and `stdout.log` extend to t=300; over t≥180, Cd=2.374685648–2.374691592 and Cl=0.428219991–0.4282388282 across 1,201 rows, without a credible shedding signal. The report describes the earlier t=64.9 incomplete state (`report.tex:447–455,623–626`); the terminal response and later workspace output say t=300. This discrepancy does not alone establish synthetic force or residual history. Re200 case score changes 2→1.

Separately `main.cpp:76–104` broadcasts the global mesh to all ranks and rebuilds geometry, then :191–199 constructs local meshes and calls solve while `gmesh` remains in scope. This violates TASK.md:227 prohibited shortcut 7; METIS and local solution state exist, so no numbered trigger 9 full-state/geometric-only claim is made. Full rubric, review areas and other cases were subsequently reviewed as described here. No CFD rerun.


## Partial steady-case calibration — 2026-10-30

Seven steady case scores in canonical order change 5,5,5,2,2,2,2 → **2,3,3,1,2,1,1**. The first three report cases include numeric force summaries, histories and Mach contours; the later workspace histories show M0.15 inviscid hit its step cap with Cd 0.014316–0.049673 over its last 10%. The report lists the remaining four cases as pending and supplies no case-specific plots or numerical summaries, while later workspace results show M0.15 laminar Cd=0.351849, M2 laminar Cd=5.722734 and Re20 Cd=3.565314 with Cl=0.148953. The report and later workspace are distinct snapshots, and no claim of synthetic data follows. Individual notes include tails and original figure references. Full rubric and review areas were subsequently reviewed; no CFD rerun.


## Full source and rubric calibration — 2026-10-30

Immutable `main.cpp:35–42` accepts `--restart` but never uses it. `mesh.cpp:58–100` supports MIXED TRI/QUAD, while its multizone mapping at :221–238 is specialized. `numerics.cpp:66–95` uses primitive-gradient chain rules and :472–484 computes tangential wall traction; these methods exist even though several results are physically poor. The report honestly discloses its t=64.9 partial transient and absence of inner-residual monitoring; later workspace metadata nevertheless asserts perfect convergence and periodic success, making the metadata finding distinct from the report snapshot. No actual strict miss count can be inferred. The full trigger assessment and 27 review points are in the scorecard. No CFD rerun, exhaustive authorship audit or pixel-level verification was performed.

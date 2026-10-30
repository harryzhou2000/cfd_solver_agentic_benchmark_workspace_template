# Evaluation — codex_generic_sonnet5-02_6c0ac0

Submission `codex/generic/sonnet5-02` at `e49016dd97c7f3fe9ae2ae1978e8ff8b9aa507a6`; checkpoint `1185ccd8b1790a6c067be86d27af84a5e1cf74fa`.

## Verdict

**75/100; DQ=true (trigger 7).** The selected M015 inviscid and M200 laminar steady forces continue to drift despite statistically-periodic success labels. Re20 and Re200 are explicitly reported failed; their failure alone is not charged as false success. Further source and DQ review remains partial.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. A clean immutable-source build succeeded; no production CFD or Re200 rerun was performed.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: dc6ff7f2f262c83c7128cd1f632ffee4ae295dade484bf930f15590a8760722e. contestant_final_response.md SHA-256: 92b9e70080c192bdf41a89fc02dbb4d9fcedeea5d4e676bfd32d43492f81d70b. These unindexed sidecars are not validated by cfdeval check.


## M015 inviscid false success — 2026-10-30

The prior DQ7 finding said no false-success evidence was established. Selected checkpoint `1185ccd:solver/results/naca0012_m015_inviscid/metadata.json` (SHA256 `038c0dcd8f1902255e3a411d77a58a2f328e1ae7aa63f6079ecf58270d0b69b3`) instead records `completed=true`, `statistically_periodic`. Its force CSV SHA256 `8613113c292b69c738b5b35d8a9f5df4c6ead7dd067b9a6f0df49049d76ab511` matches workspace. Drag descends from +0.510818 at step12001 to −0.054502 at step20000, including −0.026512→−0.054502 in the final400; lift changes −0.03513→−0.016532 over the final4000. The contestant-delivered force plot visibly shows the continuing nonperiodic drift. `report.tex:21,875` and `run_manifest.md:23,32–35` present the steady case as converged/statistically periodic. DQ7 false→true and case score2→1 for physically unusable, falsely successful result. The separately failed Re20/Re200 labels are not treated as false success merely because they failed. Other source, case and DQ review remains partial; no CFD rerun.


## Delivered standalone plots and other case windows — 2026-10-30

The previous case notes asserted that no plots were available because the curated tip excludes figure blobs. The contestant checkpoint `1185ccd` actually includes each case's standalone force, residual and Mach PNGs and `figure_manifest.csv`; all eight corresponding figure and force-history bytes match the workspace. The accepted 19-page main PDF contains text and tables but no case plots. The standalone plots were visually inspected and are contestant-delivered evidence; curated-tip reproducibility remains separately limited.

Selected last4000/1000 force windows, immutable force SHA256 values, and physical limitations are recorded in each case note. M200 laminar is called a verified stable plateau at step8000 of configured50000, yet Cd changes 0.103284→0.122727 over the final4000 steps and 0.125419→0.122727 over the final1000; its statistically-periodic success adds independent support to DQ7. M015 laminar has Cd0.786685→0.759782 over final4000 and a large force; other cases have their own limits, including tiny near-zero lift. The six remaining scores are retained on their numerical merits, not a committed-plot cap. Re20 and Re200 remain zero for explicitly disclosed failed/incomplete runs; failure alone is not charged as DQ7. No CFD rerun.


## Build, restart, force split and rubric — 2026-10-30

Immutable `e49016d` clean build succeeded in network-disabled `cfd-bench` with two CPUs, 3 GiB and 256 pids, using the documented external roots. CTest reports no registered tests; standalone test sources exist under `solver/tools` and were not independently run. No production CFD rerun. `io/restart.cpp` loads state/time/step, but `solver_driver.cpp:487–494` initializes the previous two histories identically and applies BDF2 immediately when resuming; it does not preserve an uninterrupted transient history. Build/CLI9→8.

`solver/residual.cpp:192–203` accumulates the entire wall viscous traction into the force vector; only the separate surface Cf uses a tangential projection. Report.tex:472–477 claims the force uses tangential-only shear and never normal traction. This is a force-split defect under rubric 5.4; as in prior comparable cases, a claimed output produced through a different code path is not mechanically charged as a missing algorithm under DQ6. Viscous9→7.

`time_integrator.cpp:291–331` measures spatial+BDF residual in the inner loop. The selected Re200 is honestly failed at step74/t0.74 with 74/74 target misses. `solver_driver.cpp:689` writes the midpoint of observed minimum/maximum inner iterations as `typical_inner_iterations`, not the actual mean: for M200 laminar the observed raw mean is25.543 versus metadata20. This establishes an incorrect mean proxy, not a fabricated miss fraction. Implicit/transient12→10.5. Case-results5→3.5 for documented force drift/failed cylinders, and report/visualization5→3 for false steady periodic-success claims, incorrect force-split description and the main PDF's absence of plots (standalone figures are delivered). Total83→75. Other source and DQ review remains partial.

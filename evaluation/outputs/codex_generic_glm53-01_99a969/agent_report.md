# Evaluation — codex_generic_glm53-01_99a969

Submission `codex/generic/glm53-01` at `b962bf5bfa815f4bf65d8ffc05b5734a7ac53ceb`; checkpoint `4f9bcec060022de9ff0cc8ee0ea615c2853d49c9`.

## Verdict

**84/100; DQ=false (partial calibration).** Eight packages and report pass the explicit validator; run manifest and report provide complete production evidence.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: d4b9510bf139dbf7e6461db332255a1c36271e9a34080b1474019183ff6fd1c0. contestant_final_response.md SHA-256: 916ed53fd7ed32b6b9252a7f8f766156bcf65990046b308128a1f9dbc0a3cb5d. These unindexed sidecars are not validated by cfdeval check.


## Partial case-floor and provenance review — 2026-10-30

Visually inspected all seven immutable b962bf5 solver/report/figures/<case>_forces.png plots: CL/CD histories all collapse to vertical lines at physical time zero. plot_results.py:96–110 uses physical_time as x coordinate for steady cases; searched complete report and workspace figure names and found no alternative delivered steady force-history plots. Numeric force/residual histories do exist and case-specific tail values are recorded in agent_scores.json, but the current case floor requires readable contestant-delivered histories. Seven steady scores are capped: M015/M080 inviscid 5→2, M2 inviscid 4.5→2, M015/M080 laminar 5→2, M2 laminar 4.5→2, and Re20 5→2. The Re20 stable final Cd≈0.847/Cl≈−0.390 is also physically inaccurate for a symmetric low-Re cylinder, candidly disclosed in report.tex:28–30. No physical rerun.

Re200 prior note falsely asserted no committed St: report.tex:632–643 and included re200_stats.tex give St0.11281. Visually inspected committed Re200 force image: startup Cd≈30 makes late Cd oscillations unreadable; separate lift_history.png contains lift only. Score remains2 because readable late lift AND drag plot is missing. Raw result metadata identifies bf6e816; its fv_solver.cpp differs from final b962bf5 in adaptive limiter freeze and force-stationarity thresholds. Do not assume final source generated historical outputs. Overall 94-point rubric, DQ and remaining numerical/source review remain partial.


## Partial numerical and build calibration — 2026-10-30

Exact b962bf5 source clean CMake build succeeded in network-disabled cfd-bench (`--cpus 2 --memory 3g --pids-limit 256`, CMake CFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install and build -j2). Metadata on all selected runs records bf6e816, whose fv_solver.cpp differs from final b962bf5. bf6e816 transient :893–926 refreshes gradients/limiter before each physical step and evaluates inner ratios with those coefficients frozen; zero reported Re200 misses are evidence for that lagged residual, not proof of fully refreshed nonlinear convergence. Every selected steady metadata reports inner_target_converged_fraction=0, e.g. Re20 misses794/794, M015 inviscid1034/1034. The outer residual and force-plateau criteria separately govern steady status, so missed inner targets alone do not establish DQ7. Implicit/transient rubric 14→9.

Independent FFT on submitted Re200 force history reproduces St≈0.112809 for the report’s t≥105 window, but t≥100 gives dominant peak≈0.074993 amid multiple comparable peaks. Mean Cd1.126457 and lift rms0.026634 over t≥100 match the report. This window sensitivity limits a firm shedding-frequency assessment. Re20 selected final Cd0.84691/Cl−0.38970 is physically inaccurate/asymmetric despite a stable final tail; report discloses. Case results rubric 9→6, report/visualization 4→2 for seven collapsed steady histories and missing readable late Re200 drag; rubric 94→84. DQ and other rubric/source criteria remain partial; no CFD rerun.

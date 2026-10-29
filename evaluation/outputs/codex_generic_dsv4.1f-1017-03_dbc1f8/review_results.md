# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T09:30:59.047361+00:00

**Overall score: 3.7**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 5 | Explicit official validator accepted the eight submitted workspace case directories and report; metadata, force/residual histories, surface, final VTK, partition diagnostics, restart and status files are present. Curation excluded generated data from the immutable tip while keeping original workspace evidence. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 5 | Explicit official validator invocation on all eight canonical workspace result directories and the report exited 0. This confirms structure only, not physical correctness. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 4 | All eight original result packages and report pass the structural validator. Re200 reaches t=300; its full-history force plot does not make late drag periodicity readable, and M0.15 inviscid uses disclosed first-order fallback. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 3 | Case-specific notes review forces and contours. M0.15 inviscid Cd=0.06636 with first-order fallback is materially dissipative; Re200 has a diffuse wake and low-amplitude lift, with no readable drag zoom. Steady rank comparisons are short continuations. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 3 | Main report PDF has case-specific residual, force, surface, Mach and pressure figures; Re200 wake view is present. Re200 force plot at report.tex:360 does not resolve the late drag oscillation on its full-time scale. Figure existence/appearance does not by itself establish source traceability. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 2 | Report covers methods, cases, rank study and discloses M0.15 first-order fallback. It says viscous-force split uses only tangential traction (report.tex:292,312), while solver.cpp:586–608,1082–1110 includes the full stress vector. It also describes a generic 0.5 slope while Re200 script/metadata use 0.8; late-time drag variation is unreadable in its figure. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 4 | Submitted report/figure_manifest.csv maps images to per-case fields and histories; final force values checked against forces.csv. report.tex:26–27 and :554–557 presents 0.5 as a generic production slope whereas Re200 script and metadata use 0.8; full figure-to-array quantitative verification was not performed. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 3.7
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

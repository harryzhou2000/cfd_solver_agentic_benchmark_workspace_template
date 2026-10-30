# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T16:53:03.254453+00:00

**Overall score: 0.45**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 1 | Three complete-looking packages explicitly failed; Re20 metadata/status contain trailing invalid JSON. Four more histories partial, Re200 empty. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 0 | No submitted validator pass; missing completed cases and report assets preclude a passing submission. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 0 | Three final statuses failed, four histories partial, and Re200 empty; completed true does not override explicit failed status. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 0 | Re20 last1000 CD -58.94 to 66.42; NACA late histories have extreme spikes. Concurrent launches and unbound binaries prevent attributing a single cause. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 0.5 | Fourteen delivered line PNGs are dominated by extreme spikes; PDF contains no figures and no domain contour or Re200 wake. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 1 | Report gives equations and admits heat limitation, but falsely asserts eight complete cases, unsupported Re200 St≈0.19, and non-existent skin-friction force decomposition. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 0.5 | Manifest names bare force/residual CSV paths, while PDF embeds no figures; run_manifest contains only its header. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 0.45
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

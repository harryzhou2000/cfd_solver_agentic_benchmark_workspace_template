# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-08-14T07:00:42.876445+00:00

**Overall score: 0.0**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 0 | Immutable submission contains no required canonical solver/results case directory; all eight required result packages are absent. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 0 | Eight canonical paths are absent, so an explicit validator pass cannot be credited; summary inventory finds only four noncanonical uncommitted test directories. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 0 | No submitted final result package supports completion, convergence, or statistically-periodic claims for any required case. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 0 | No committed residual/force/surface/field data or rank comparison exists to inspect. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 0 | report.tex references eight figure files, but immutable commit contains no report/figures PNGs. The submitted report therefore cannot build with its claimed visualizations and no visualization evidence is delivered. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 0 | LaTeX methods/tables assert all eight runs and complete output while no result packages or referenced figures are committed. The report is not build-complete and claims lack immutable trace data. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 0 | No submitted figure_manifest, run_manifest, sanity_checks, result CSV, or field files connect report claims to immutable outputs. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 0.0
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

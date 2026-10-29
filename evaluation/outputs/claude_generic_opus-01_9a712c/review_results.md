# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-29T11:22:18.914295+00:00

**Overall score: 4.03**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 4.5 | Explicit validator passes all eight original result packages and report. The binary restart contains current physical state but not the previous BDF2 time level needed for an accurate transient continuation. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 5 | Explicit benchmark validator passed eight original packages and the report; structural validation does not establish numerical accuracy or verify all report claims. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 4 | Re200 reaches t=300 with 0 reported strict inner-target misses; seven steady packages report converged. M2 laminar drag still rises from 0.1117185 to 0.1146643 over the last 1,223 force rows. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 4 | Re20 Cd=2.0172 and Re200 St=0.1804 are broadly plausible; M0.15 inviscid numerical drag is 8.77e-4. M2 laminar Cd drifts 2.57% over the last 10% of rows; rank-study results are closely matched. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 3.5 | Case-specific residual, force and field figures are present; Re200 late lift/spectrum is readable but its full-time Cd plot is flattened by startup, and steady laminar drag tails are also compressed. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 3.5 | Detailed report states methods and limitations but report.tex:664–679 mixes Re200 mean total with final-time force components, and :1232–1237 incorrectly calls M2 normal viscous drag several orders below pressure drag. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 3.5 | figure_manifest.csv maps plots to case outputs; tools/plot_results.py:176–183 reads VTU fields. Re200 table components do not match the stated averaging window; figure pixels were not exhaustively compared to source arrays. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 4.03
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

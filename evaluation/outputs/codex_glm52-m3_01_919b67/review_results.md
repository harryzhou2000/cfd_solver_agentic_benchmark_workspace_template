# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T09:01:28.684988+00:00

**Overall score: 2.85**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 4 | Prior historical validator reported all eight layouts passing, but status claims for the M2-independent laminar and Re20 results are inconsistent with the selected force histories; correct schema is not convergence. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 5 | Prior historical eight-directory validator exited 0; that establishes structural output compliance only. Evaluator clean build succeeded without a CFD rerun. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 2 | Checkpoint 52a1e520: all three laminar NACA forces end at Cd≈405–532 and still rise; Re20 Cd≈2028; Re200 final ratio0.156 misses0.001. Metadata marks these successful despite their distinct limitations. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 1 | Checkpoint 52a1e520 laminar NACA final Cd≈532/458/405 and still rising, Re20 Cd≈2028. Re200 final inner ratio misses target. NACA inviscid/rank comparisons remain under separate review. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 3.5 | 52a1e520 contains 56 separately delivered manifest-listed figures, although five-page curated report omits them. Re200 force graph has readable late lift but late drag is compressed by startup extremes. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 2.5 | 6b376564 report discloses 10-inner cap and underdeveloped laminar cases, but asserts steady second order after step200 and presents an incorrect rank comparison table; its Re200 St estimate has no numeric value. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 2 | 52a1e520 includes raw plots and history manifest, but report.tex:159–166 misstates np2/4/8 step200 Cd≈0.116; checkpoint force data give 0.5071/0.5065/0.5080. The 0.116 value belongs to np2 step2000. No specific figure mismatch or fabrication inferred. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 2.85
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

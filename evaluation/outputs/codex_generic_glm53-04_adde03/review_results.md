# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-10-30T18:23:27.179879+00:00

**Overall score: 3.3**


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 | 4 | Examiner validator recorded for all eight selected final packages; source restart serializes state levels but lacks physical time/step. Validator structure does not prove convergence or accuracy. |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 | 4 | Structural examiner validation is recorded in the snapshot; manual Re200 plot and wake estimator checks, M2 laminar force stability and source provenance identify limitations beyond a validator pass. |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 | 3 | M2 laminar metadata claims converged while final 40 Cd values fall 0.853765→0.529150; Re200 t300 and plotted late drag limitation are distinct. Selected historic outputs are not bound to immutable solver source. |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 | 3 | Re20 last Cd≈2.072 is stable; Re200 raw lift FFT≈0.175. Rank-study NACA np1 vs np8 final Cd 0.001153759 vs 0.001167319 and Cl −0.0012681 vs +0.0006058; Re20 Cd 2.071663 vs 2.071655. Report claim of solver-roundoff agreement overstates NACA agreement; absolute differences do not demonstrate an order-one force mismatch. |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 | 3 | Delivered force histories and contours are readable, with compressed late drag. The Re200 vorticity wake figure is pixel-reproduced from selected field with the committed plotting helper; the helper fails an exact solid-rotation check and disagrees with an independent local-neighbor wake diagnostic. This is a derived-variable accuracy defect, not proof of a substituted field. |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 | 3 | c06741d report.tex:294–315 labels steady table values final though M2 Cd1.025 matches last100 mean rather than final0.529; text:445 gives 2.162. Rank agreement claim:529–533 overstates NACA agreement; PDF includes disclosure of CFL/limiter limitations. |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 | 3 | The 30-page accepted report and figure manifest are delivered. Re200 wake image pixel-reproduces from selected field and final plotting helper, which is mathematically defective; case metadata revision26ef1cb does not bind production solver source. |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): 3.3
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:

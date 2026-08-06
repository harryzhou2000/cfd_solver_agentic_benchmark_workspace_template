# Result review scorecard (outputs, visualizations, report)

Score scale: 0-5 per point; weights are fractions of the area total.
Overall score = weighted mean of point scores (weights shown).
Generated: 2026-08-06T20:16:04.718423+00:00


| # | Point | Weight | Score (0-5) | Evidence / notes |
|---|-------|--------|--------------|------------------|
| 1 | **res.contract** Output contract compliance | 0.20 |  |  |
|   | Every scored case output dir contains metadata.json, partition_diagnostics.csv/.json, residuals.csv, forces.csv, surface.csv, field_final.*, restart_final.*, stdout.log, run_status.json with required headers/schema; completed=true; run_status matches reality. | | | |
|   | *Evidence:* Directory listing per case; header check; validate_outputs.py --report. | | | |
| 2 | **res.validator** Automated validator results | 0.10 |  |  |
|   | validate_outputs.py passes for the submission; validator run included in the workspace or report; any failures explained or fixed. | | | |
|   | *Evidence:* Validator output/log in workspace. | | | |
| 3 | **res.completion** Case completion and convergence | 0.20 |  |  |
|   | All required NACA0012 and cylinder cases completed: inviscid converged/plateaued, laminar converged, Re 20 steady wake, Re 200 statistically periodic; no placeholder/partial/synthetic result files; final rows finite and consistent. | | | |
|   | *Evidence:* run_status.json + metadata.json per case; residual/force tails; result_statistics.json if present. | | | |
| 4 | **res.plausibility** Result plausibility and MPI consistency | 0.15 |  |  |
|   | Min density/pressure positive; inviscid viscous forces negligible; wall velocities near zero; force histories comparable across np=1/2/4/8; Re 200 shows vortex street and shedding frequency/Strouhal estimate. | | | |
|   | *Evidence:* Forces/residuals/surface CSVs; rank comparison dirs; report analysis. | | | |
| 5 | **res.visualization** Visualizations | 0.15 |  |  |
|   | Mach AND pressure computed-field figures for main body cases with correct filenames, colorbars, captions, adequate resolution (not scatter-only/tiny/unlabeled); line plots publication-style with labeled axes/legends; Re 200 wake-focused vorticity/velocity figure. | | | |
|   | *Evidence:* Figures dir + figure_manifest.csv; render a sample; compare with source data. | | | |
| 6 | **res.report** Report quality and honesty | 0.15 |  |  |
|   | Governing equations, nondimensionalization, BCs, schemes accurately described with formulas; histories for all cases; convergence/vortex-street/MPI/limitations analyzed honestly with tables and figure references; reproducible artifacts. | | | |
|   | *Evidence:* report.pdf/report.tex review; cross-check claims vs metadata and CSVs. | | | |
| 7 | **res.traceability** Traceability of figures and claims | 0.05 |  |  |
|   | Every figure tied to source data via manifest or equivalent; every reported coefficient traceable to forces.csv; residual reduction traceable to residuals.csv; final forces row corresponds to final field/surface state. | | | |
|   | *Evidence:* figure_manifest.csv; spot-check values in text vs CSVs. | | | |

## Disqualification flags

- [ ] Figure files misnamed (e.g., Mach image that actually plots pressure)
- [ ] Report figures do not correspond to submitted field/CSV outputs
- [ ] Placeholder, synthetic, or intentionally partial result files in scored case dirs
- [ ] metadata.json marks incomplete or failed runs as successful
- [ ] Debug outputs presented as final case results

## Summary

- Overall score (0-5): ____
- Key strengths:
- Key weaknesses:
- Disqualification triggered? yes/no — explain:


## Structural evidence (automated)

- Required cases: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_inviscid, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Found case dirs: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, cylinder_m010_laminar_re20_np1, cylinder_m010_laminar_re20_np2, cylinder_m010_laminar_re20_np4, naca0012_m015_inviscid, naca0012_m015_inviscid_np1, naca0012_m015_inviscid_np2, naca0012_m015_inviscid_np4, naca0012_m015_inviscid_np8_n, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Missing cases: none
- Figure manifest: {"manifest_present": true, "entries": 49, "missing_figures": [], "missing_sources": []}
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}}
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic csv={'residuals.csv': {'header_ok': True, 'rows': 778306, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 30000, 'last_row_finite': True}}
- `naca0012_m015_inviscid_np8_n`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}}
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}}
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 6261, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 6261, 'last_row_finite': True}}
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 3944, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 3944, 'last_row_finite': True}}
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 6185, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 6185, 'last_row_finite': True}}
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 5712, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 5712, 'last_row_finite': True}}
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 8597, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 8597, 'last_row_finite': True}}
- `cylinder_m010_laminar_re20_np1`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}}
- `cylinder_m010_laminar_re20_np2`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}}
- `cylinder_m010_laminar_re20_np4`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 4186, 'last_row_finite': True}}
- `naca0012_m015_inviscid_np1`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}}
- `naca0012_m015_inviscid_np2`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}}
- `naca0012_m015_inviscid_np4`: missing=[] completed=True status=converged csv={'residuals.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}, 'forces.csv': {'header_ok': True, 'rows': 2785, 'last_row_finite': True}}

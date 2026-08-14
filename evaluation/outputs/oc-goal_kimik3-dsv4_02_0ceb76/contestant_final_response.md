Benchmark task complete. Final state:

- **All 8 cases pass the examiner validator**: 7 steady cases converged (2.3–5.0 orders), Re200 transient statistically periodic with a clean von Kármán street (St=0.173, cd≈1.22, cl∈[-0.46,0.48], 100% inner-loop convergence)
- **Report validated**: 14-page report.pdf (0 LaTeX errors), run_manifest.md, sanity_checks.json (all_passed=true), 66 figure manifest entries
- **Key fixes this session**: metadata nullptr-json crash; per-cell under-relaxation (fixed the M2 Mach-4000 divergence); GS sub-iterations + limiter freezing in the BDF2 inner loop (fixed the 1e-3 inner-residual stall); vorticity plotting over duplicated VTU nodes
- All committed on `solver/kimik3-main` (latest: dd0fabf)

Documented limitations (in report): low-Mach LLF over-dissipation inflates laminar drag; m080_inviscid sits in a 2.3-order limiter-shock limit cycle with stable forces.

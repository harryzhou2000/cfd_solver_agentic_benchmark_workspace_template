The benchmark is complete and fully validated. Everything lives in `/workspace/solver/` on branch `solver/cfd2u`, with the benchmark submodule untouched.

**What was finished this session**

- Fresh uncontended scaling study (np=1/2/4/8 for one NACA + one cylinder case): speedups 2.0/4.1/3.1 and 2.0/3.5/3.3 — the previously committed numbers had been measured under machine contention. Fixed a macro-generator bug that dropped the edge-cut/load-balance values.
- Regenerated all 48 figures and the three manifests; the Re200 vorticity contour (clipped [−5,5]) shows a genuine Kármán street, and the post-transient force window shows sustained Cl ±0.35 oscillation at St≈0.168, mean Cd 1.162.
- Extended `sanity_checks.json` to explicitly cover all 9 contract physics-gate checks (field positivity, cp variation, no-slip/slip wall semantics, NACA symmetry, cylinder mean drag, Re200 unsteady lift, mach/pressure figure mapping) — all flags pass for all 8 cases.
- An independent k3 review of the report against `report_requirements.tex` and `OUTPUT_CONTRACT.md` initially returned **FAIL** with 14 findings (missing per-case history/Cp/Cf figures in the tex, wall-time table contradicting the manifests, partition table missing send/recv, and more). I fixed all of them — per-case figure galleries, corrected tables, numeric steady parameters, a visualization-style section — plus 4 editorial nits from the re-review, which returned **PASS**.
- Fixed a reproducibility gap: `run_manifest.csv` now records the real benchmark-relative case paths, verified to work verbatim, and the README shows a concrete invocation. Error paths exit nonzero; `--restart` verified.

**Final state of evidence**

- Examiner validator: `OK` for all 8 case directories and `OK report`.
- All 8 cases `completed=true`: 7 steady converged (4.0–6.4 orders residual reduction with force-plateau gate), Re200 `statistically_periodic` with honest metadata (`true_bdf2_inner_loop`, 49–93 inner iterations, 0 target misses, 30000 steps to t=300).
- `report.pdf`: 18 pages, zero LaTeX errors/undefined references; all 48 figures referenced in text and mapped in `figure_manifest.csv`.
- Work committed on `solver/cfd2u` (latest `a53b2f9`); benchmark repo pristine (0 modifications).

One documented deviation remains, justified in the report: transient pseudo-CFL 4.0 vs the suggested 1.0 (inner target met at every step), and Venkatakrishnan K=10 for Re200 to counter Rusanov's low-Mach dissipation killing the street — both analyzed in the deviations and limitations sections.
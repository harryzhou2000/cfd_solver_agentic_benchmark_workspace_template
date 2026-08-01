# Result Review Specification (outputs, visualizations, report)

## Purpose

Score what the submission produced: contract-compliant outputs, physical
plausibility, visualization quality, and report quality/honesty, per
`OUTPUT_CONTRACT.md`, `report_requirements.tex`, and the examiner guide.

## Method

- Point list: `evaluation/config/review_points_results.json`.
- The pipeline performs structural checks automatically (files present,
  headers, metadata fields, figure manifest) and emits them into
  `review_results.md` as evidence; reviewers score quality aspects 0–5.
- **Layout discovery is explicit**: contestant layouts may be non-standard
  (e.g., solver/results/report inside the benchmark submodule directory
  rather than a workspace `solver/`). The tool searches candidate locations
  and records what it found in `contestant.layout` and the `results_dir` /
  `report_dir` fields instead of assuming a fixed path.

## Points

1. **res.contract — Output contract compliance** (0.20): per-case required
   files and schemas (`metadata.json`, `partition_diagnostics.*`,
   `residuals.csv`, `forces.csv`, `surface.csv`, `field_final.*`,
   `restart_final.*`, `stdout.log`, `run_status.json`); `completed=true`;
   headers correct.
2. **res.validator — Automated validator results** (0.10): `validate_outputs.py`
   passes; evidence present in the workspace.
3. **res.completion — Case completion and convergence** (0.20): all required
   cases completed with converged/plateaued steady runs and a statistically
   periodic Re 200; no placeholder/synthetic/partial files.
4. **res.plausibility — Result plausibility and MPI consistency** (0.15):
   positive density/pressure; negligible inviscid viscous forces; near-zero
   wall velocities; force histories comparable across rank counts; Re 200
   vortex street with Strouhal estimate.
5. **res.visualization — Visualizations** (0.15): Mach + pressure field
   figures for main body cases (correct filenames, colorbars, captions,
   adequate resolution); publication-style line plots; Re 200 wake
   vorticity/velocity figure.
6. **res.report — Report quality and honesty** (0.15): equations,
   nondimensionalization, BCs, schemes; histories; honest analysis of
   convergence/vortex street/MPI/limitations; reproducible artifacts.
7. **res.traceability — Traceability** (0.05): figures tied to source data
   (manifest); reported coefficients traceable to CSVs; final force row
   matches final field/surface state.

## Disqualification flags

Misnamed figures; figures not matching submitted outputs; placeholder/
synthetic/partial results; failed runs marked successful; debug outputs as
final results.

## Structural checks the pipeline runs

- For each discovered case results dir: presence and header check of the
  contract files; `completed`/`convergence_status` from `metadata.json`;
  final-row finiteness of `residuals.csv`/`forces.csv`; `run_status.json`
  consistency.
- Report dir: `report.pdf`/`report.tex` presence; `figures/` listing;
  `figure_manifest.csv` (if present) cross-referenced against the figures
  dir and referenced source files.
- If the contestant produced `result_statistics.json`, its case list is
  compared against the required case list.

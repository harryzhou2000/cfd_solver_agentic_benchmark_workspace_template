# Result analysis and report generation

The post-processing pipeline consumes only actual solver outputs and does not
create or alter CFD results. It generates publication-style line plots and
cell/point field contours, quantitative summaries, traceability manifests, the
machine-readable physics sanity gate, and a LaTeX report.

## Dependencies

Python 3.10 or newer with NumPy and Matplotlib is required. Install these in an
environment outside the repository, or use the benchmark environment:

```bash
python3 -m pip install -r solver/tools/requirements.txt
```

`pdflatex` is optional. ASCII VTU and legacy ASCII VTK field files are
supported. Field data must include density (or `rho`), pressure (or `p`), Mach
number, velocity components/vector, and optionally vorticity.

## Generate artifacts

After all eight case directories exist under `solver/results/`, run from the
workspace root:

```bash
python3 solver/tools/report.py \
  --results solver/results \
  --report solver/report \
  --compile
```

For an explicitly labeled interim report, add `--allow-partial`. The normal
command refuses incomplete case sets. Generated files are:

```text
solver/report/report.tex
solver/report/report.pdf              # if pdflatex is available
solver/report/analysis.json
solver/report/run_manifest.csv
solver/report/mpi_comparison.json
solver/report/sanity_checks.json
solver/report/figure_manifest.csv
solver/report/figures/*.png
```

The generator makes residual and force histories, surface pressure plots, and
separate Mach and pressure field images for every case. Cylinder cases also get
a velocity-magnitude or vorticity wake image. Field plots use actual
unstructured cells (or triangulated point data), never scatter-only rendering.
Color limits use the 1st and 99th percentiles; vorticity is additionally clipped
at `[-5, 5]` when needed.

The generated `report.tex` follows `cfd_solver_agentic_benchmark/report_requirements.tex`:
it includes governing equations, nondimensionalization, mesh and boundary tables,
exact production controls, reconstruction/limiter fallbacks, BDF2 history
freezing, METIS edge-cut and load-balance diagnostics, force splits, cross-
referenced figure panels, rank-count analysis, and an explicit limitations
section. When `pdflatex` is installed, `--compile` emits the corresponding
`report.pdf` after two passes so table and figure references resolve.

The report also compares the independent full-horizon `np=1` runs in
`solver/parallel_runs/naca_np1` and `solver/parallel_runs/cylinder_np1` with
the submitted `np=8` production runs. `mpi_comparison.json` records the exact
commands, timings, final force differences, and pass criteria for both cases.

## Validation

Run both the independent submission checker and the benchmark examiner:

```bash
python3 solver/tools/validate_submission.py \
  --results solver/results --report solver/report

python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  solver/results/naca0012_m015_inviscid \
  solver/results/naca0012_m080_inviscid \
  solver/results/naca0012_m200_inviscid \
  solver/results/naca0012_m015_laminar_re5000 \
  solver/results/naca0012_m080_laminar_re5000 \
  solver/results/naca0012_m200_laminar_re5000 \
  solver/results/cylinder_m010_laminar_re20 \
  solver/results/cylinder_m010_laminar_re200 \
  --report solver/report
```

The sanity thresholds are deliberately transparent in `report.py`. A failed
gate remains failed; the tooling never changes a solver's convergence status.

# 2-D Unstructured Compressible Navier-Stokes Solver

Original C++17/MPI finite-volume solver for the
`cfd_solver_agentic_benchmark` cases: NACA0012 (inviscid and laminar,
Re 5000, M = 0.15/0.8/2.0) and cylinder (laminar Re 20 steady and Re 200
transient vortex street). All eight required cases were run to converged or
statistically periodic states; see `solver/report/report.pdf` for the full
analysis.

## Repository layout

```text
solver/
  CMakeLists.txt       build system
  src/                 C++17/MPI solver source
  tools/               launch scripts and plot_results.py
  report/              report.tex, report.pdf, figures/, manifests, sanity checks
  results/             final result packages for the eight required cases
  .venv/               Python virtual environment (numpy, matplotlib)
cfd_solver_agentic_benchmark/   read-only benchmark input (cases, meshes, examiner)
external/              DNDSR-style external libraries (CGNS, HDF5, METIS, Eigen, ...)
```

## Dependencies

- Build: CMake >= 3.20, a C++17 compiler (GCC 13 used), OpenMPI
  (`/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin` on the build
  machine; any MPI-3 implementation works), and the compiled external
  libraries under `external/cfd_externals/install/` (CGNS, HDF5, METIS).
- Python (3.10+): `numpy` and `matplotlib`, installed in `solver/.venv`
  (see the setup command below). All Python tooling is run with the venv
  interpreter.
- LaTeX (optional): `latexmk`/`pdflatex` to rebuild the report PDF.

## Build

```bash
cmake -S solver -B solver/build
cmake --build solver/build -j
```

The default externals root is `solver/../external/cfd_externals/install`
(the DNDSR-style resource layout); override it with an absolute path via
`-DCFD_EXTERNALS_ROOT=<abs path>` if the resources live elsewhere. Paths are
not hard-coded into the solver.

## Python environment (one-time setup)

```bash
python3 -m venv solver/.venv
solver/.venv/bin/pip install numpy matplotlib
```

## Run

The solver accepts the benchmark contract CLI:

```bash
mpirun -np 8 ./solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/<case>.json \
  --output <out_dir> --report-level full
```

where `<case>.json` is one of the eight case files in
`cfd_solver_agentic_benchmark/inputs/cases/`. No source or input edits are
needed between cases. The output directory receives `metadata.json`,
`run_status.json`, `residuals.csv`, `forces.csv`, `surface.csv`,
`field_final.vtu`, `partition_diagnostics.csv`, `restart_final.*`, and
`stdout.log` per the output contract.

For reproducible pinned-core launches (as used for the submitted results),
see `solver/tools/launch_pinned.sh <np> <first_cpu> <case_json> <out_dir>`.
Optional runtime controls are documented in the report, e.g. the
credible-plateau settings (`CFD_PLATEAU_MIN_ORDERS`,
`CFD_PLATEAU_WINDOW`, `CFD_PLATEAU_CONFIRM`) and the LU-SGS/block-Jacobi
inner-solver selection (`CFD_USE_LUSGS`).

## Plots, validation, and report

Regenerate all figures (and `figure_manifest.csv` + `sanity_checks.json`):

```bash
solver/.venv/bin/python solver/tools/plot_results.py \
  solver/results solver/report \
  solver/results/naca0012_m015_inviscid ...  # all eight case directories
```

Run the benchmark contract validator:

```bash
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

Rebuild the LaTeX report:

```bash
cd solver/report && latexmk -pdf -interaction=nonstopmode report.tex
```

## Results summary

All steady cases reached `converged` (credible force plateau); the Re 200
cylinder reached `statistically_periodic` vortex shedding (St ~ 0.275,
mean C_D ~ 1.91). MPI rank-count validation (np = 1/2/8) was performed for
one NACA case and one cylinder case; see the report's Parallel Validation
section.

# 2-D Unstructured Compressible Navier--Stokes Solver

From-scratch C++17/MPI finite-volume solver for the
`cfd_solver_agentic_benchmark` task: CGNS mesh import, METIS cell-graph
partitioning, neighbor-scoped halo exchange, second-order MUSCL-type
discretization (weighted least-squares reconstruction, Barth--Jespersen
limiter with positivity fallbacks, Rusanov flux, Newtonian viscous terms),
damped block-Jacobi implicit marching, and a true BDF2 dual-time transient
loop.

## Dependencies and build

Required toolchain:

- CMake >= 3.16 and a C++17 compiler
- OpenMPI (or another MPI-3 implementation) with `mpicxx`
- Compiled externals under `external/cfd_externals/install/` (the benchmark
  DNDSR convention): CGNS (built 64-bit, `CGNS_64BIT=1`), HDF5, METIS, zlib
- Header-only `nlohmann/json` under `external/nlohmann/`

The repository root contains an `external -> ...` symlink that the benchmark
environment provides; the CMake defaults resolve
`CFD_EXTERNALS_ROOT=<repo>/external/cfd_externals/install` and
`EXTERNAL_HEADERS_ROOT=<repo>/external`. Both are configurable cache
variables (`-DCFD_EXTERNALS_ROOT=... -DEXTERNAL_HEADERS_ROOT=...`).

```bash
cd solver
cmake -S . -B build
cmake --build build -j
```

This produces `build/cfd_solver` (the CLI solver) and `build/mesh_info`
(mesh/partition diagnostics tool).

## Running the cases

Every case uses the same command line (no manual edits between cases):

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/<case>.json \
  --output solver/results/<case>
```

with `case` one of:

```
naca0012_m015_inviscid        naca0012_m080_inviscid
naca0012_m200_inviscid        naca0012_m015_laminar_re5000
naca0012_m080_laminar_re5000  naca0012_m200_laminar_re5000
cylinder_m010_laminar_re20    cylinder_m010_laminar_re200
```

Production settings used for the submitted runs (documented in the report):

- Steady cases are declared converged by the credible-plateau criterion
  (windowed residual/force flatness over 2500 steps plus a per-case minimum
  residual reduction). Per-case environment overrides used:

  | case | `CFDS_PLATEAU_MIN_ORDERS` | `CFDS_PLATEAU_FORCE_TOL_ABS` |
  |---|---|---|
  | naca0012_m015_inviscid | 0.8 | 0.002 |
  | naca0012_m080_inviscid | 0.8 | 0.003 |
  | naca0012_m200_inviscid | 0.3 | 0.002 |
  | naca0012_m015_laminar_re5000 | 0.35 | 0.003 |
  | naca0012_m080_laminar_re5000 | 0.4 | 0.003 |
  | naca0012_m200_laminar_re5000 | 0.1 | 0.003 |
  | cylinder_m010_laminar_re20 | 0.1 | 0.012 |

  Example:
  `CFDS_PLATEAU_MIN_ORDERS=0.8 CFDS_PLATEAU_FORCE_TOL_ABS=0.002 mpirun -np 8 ...`

- The Re 200 transient uses the supplied production settings
  (`time_step=0.01`, `final_time=300`, `min_inner_iterations=5`,
  `max_inner_iterations=1000`, `inner_residual_reduction_target=1e-3`) with
  a true frozen-history BDF2 loop. The run additionally sets
  `CFDS_TRANSIENT_SEED=0.001`, a documented antisymmetric near-wake
  velocity perturbation that triggers the physical shedding instability
  from the symmetric start (without a seed the perfectly symmetric discrete
  state stays on the stable symmetric branch).

All other knobs come from the case JSON files. Run `cfd_solver solve` with
no arguments for usage.

## Python environment

All Python preprocessing, plotting, validation, and report-automation
scripts run inside the local virtual environment:

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

Dependencies: `numpy`, `matplotlib` (plotting/analysis only). The `.venv`
directory is git-ignored. Use the venv interpreter for every script, e.g.
`solver/.venv/bin/python solver/tools/make_figures.py`.

## Reproducing figures, report, and validation

```bash
cd <repo>
solver/.venv/bin/python solver/tools/make_figures.py solver/results \
  solver/report --mpi-dir solver/results/mpi_np2
solver/.venv/bin/python solver/tools/fill_report.py solver/results \
  solver/report --mpi-dir solver/results/mpi_np2
cd solver/report && pdflatex report.tex && pdflatex report.tex
```

`make_figures.py` writes `figures/`, `figure_manifest.csv`,
`run_manifest.csv`, and `sanity_checks.json`; `fill_report.py` regenerates
`report.tex` from the `report.template.tex` draft. The contract validator:

```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  solver/results/* --report solver/report
```

(the eight case directories are passed explicitly; the glob works from the
repo root if the shell expands it).

## Results

`solver/results/<case>/` contains the full output contract for every case
(`metadata.json`, `run_status.json`, `partition_diagnostics.csv`,
`residuals.csv`, `forces.csv`, `surface.csv`, `field_final.vtu`,
`restart_final.bin`, `stdout.log`). `solver/results/mpi_np2/` holds the
np=2 reruns used for the rank-count comparison (np=8 vs np=2 forces agree
to <3%). All steady cases are `converged`; the Re 200 cylinder is
`statistically_periodic` after 30000 BDF2 steps with the strict 1e-3 inner
target met on 97.9% of steps (last inner ratio 5.4e-4).

## Repository layout

```
solver/
  CMakeLists.txt
  src/            C++17/MPI solver (mesh, partition, fv, physics, io)
  tools/          make_figures.py, fill_report.py, plot_field.py
  report/         report.tex/template, report.pdf, figures/, manifests
  results/        per-case output packages + mpi_np2/ comparisons
  .venv/          local Python environment (git-ignored)
```

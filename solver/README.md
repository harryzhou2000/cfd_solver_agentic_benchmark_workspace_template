# cfd2d — 2-D unstructured finite-volume compressible Navier-Stokes solver

This directory contains the submitted solver for the
`cfd_solver_agentic_benchmark` task: a 2-D cell-centered unstructured
finite-volume solver for the compressible Navier-Stokes equations of a
calorically perfect gas, with MPI domain decomposition, METIS partitioning,
an approximate Riemann flux, second-order limited reconstruction, and an
implicit LU-SGS/SGS pseudo-time (and BDF2 dual-time transient) solve.

## Build

```bash
cd solver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
The build auto-detects the DNDSR-style externals at
`../external/cfd_externals/install` (CGNS, HDF5, METIS/ParMETIS, zlib) and the
header-only packages under `../external/` (Eigen, fmt, nlohmann_json). Override
with `-DCFD_EXTERNALS_ROOT=<path>`.

## Python (figures/report)

```bash
cd solver
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
# invoke plotting/report scripts with the venv interpreter, e.g.:
# .venv/bin/python tools/plot_results.py <case-output-dir> <case_id>
```

## Run a case

```bash
export LD_LIBRARY_PATH=$(pwd)/../external/cfd_externals/install/lib:$LD_LIBRARY_PATH
mpirun --oversubscribe -np <ranks> ./build/cfd2d solve \
  --case <benchmark>/inputs/cases/<case>.json --output results/<case> \
  [--restart <file>] [--report-level brief|full] [--max-steps N]
```
The mesh path in each case JSON is resolved relative to the case-file
directory, so the same executable and command works for every supplied case
with no source edits. Exit status is 0 on normal completion, nonzero on
malformed input or failed initialization.

## Run all required cases

```bash
bash tools/run_all.sh     # launches all 8 cases in parallel
```

## Generate figures + report

```bash
.venv/bin/python tools/make_report.py        # figures, manifests, sanity_checks
cd report && latexmk -pdf report.tex
```

## Numerical method summary

- Inviscid flux: Roe with Harten-Yee entropy fix (supersonic cases) or
  Rusanov/local-Lax-Friedrichs (subsonic, where Roe is low-Mach unstable).
- Spatial: cell-centered FV, Green-Gauss primitive gradients, piecewise-linear
  MUSCL reconstruction with a Barth-Jespersen limiter (conservatively capped at
  0.5 for stability with the simplified implicit) and a positivity fallback.
- Viscous: Newtonian stress + Fourier heat flux on primitive gradients;
  no-slip adiabatic wall, slip wall, Riemann farfield.
- Implicit: matrix-free LU-SGS/SGS with a full-spectral diagonal (stable in the
  high-CFL limit), nonlinear inner iterations per pseudo-step.
- Steady: CFL-ramped pseudo-time. Transient (Re 200): BDF2 dual-time with a true
  physical-time outer loop and inner nonlinear/linear iterations (histories
  frozen during inner solve).
- MPI: METIS k-way cell-graph partition, rank-local owned+ghost mesh,
  neighbor-scoped Isend/Irecv halo exchange, global reductions for
  residuals/forces. No full-mesh/full-state replication during iterations.

## Documented deviations from the supplied parameters

For stability with the simplified (scalar) implicit the steady pseudo-CFL is
capped at 2 (cases request up to 100) and the subsonic Rusanov dissipation
scale is set to 2.0 (cases imply 1.0). These are stricter/more-dissipative
settings; the report records residual and force histories and the resulting
spurious-drag level honestly.

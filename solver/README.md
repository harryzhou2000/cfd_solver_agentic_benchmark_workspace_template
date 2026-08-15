# fv2d — 2-D unstructured compressible Navier–Stokes solver (MPI)

`fv2d` is a from-scratch C++17 cell-centered unstructured finite-volume solver
for the compressible Navier–Stokes equations of a calorically perfect gas, built
for the CFD solver agentic benchmark. It features:

- CGNS mesh import (multi-zone, 1-to-1 interface merging) via the CGNS/HDF5 library
- METIS k-way graph partitioning; rank-local owned+ghost cells; neighbor-scoped
  nonblocking `MPI_Isend`/`MPI_Irecv` halo exchange (no full-mesh or full-state
  replication during iterations)
- Rusanov (local Lax–Friedrichs) approximate Riemann flux
- Second-order piecewise-linear reconstruction from weighted least-squares
  gradients, Venkatakrishnan limiter, positivity fallback
- Laminar viscous fluxes (Newtonian stress + Fourier heat flux), constant or
  Reynolds-matched viscosity
- Implicit pseudo-time integration with LU-SGS relaxation and local CFL control
- True two-level BDF2 physical-time transient integration with inner nonlinear
  iterations (frozen histories) for the cylinder Re 200 vortex street

## Dependencies

- MPI (Open MPI or compatible), CMake ≥ 3.16, C++17 compiler
- DNDSR-style externals: set `CFD_EXTERNALS_ROOT` to
  `<workspace>/external/cfd_externals/install` (provides CGNS, HDF5, METIS,
  zlib) and `CFD_HEADER_ROOT` to `<workspace>/external` (provides Eigen,
  nlohmann_json). Both default to those locations relative to this directory.
- Python 3 virtual environment for plotting/analysis:
  ```bash
  python3 -m venv .venv
  .venv/bin/pip install numpy matplotlib h5py
  ```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install \
  -DCFD_HEADER_ROOT=<workspace>/external
cmake --build build -j
```

## Run

```bash
mpirun -np <ranks> build/fv2d solve --case <case.json> --output <output-dir> \
    [--restart <restart-file>] [--report-level brief|full]
```

All eight benchmark cases run with the same executable and no input edits:

```bash
tools/run_all.sh 8                 # seven steady cases into results/
NP=8 OUT_DIR=results/cylinder_m010_laminar_re200 \
  tools/run_re200_supervised.sh    # Re200 transient (checkpointing supervisor)
```

The Re200 supervisor wraps the solver with a rolling checkpoint
(`restart_checkpoint.bin` every 2000 physical steps) so the 30000-step run is
resumable after an interruption; CSV histories are truncated/continued
consistently on restart.

## Outputs

Each case output directory contains `metadata.json`,
`partition_diagnostics.csv`, `residuals.csv`, `forces.csv`, `surface.csv`,
`field_final.vtu`, `restart_final.bin`, `stdout.log`, `run_status.json`
(contract-compliant), plus `partitions_np<N>/` (cached rank-local meshes).

## Analysis and report

```bash
.venv/bin/python tools/analyze.py --results results --report report
.venv/bin/python tools/make_figures.py --results results --report report
.venv/bin/python tools/make_report_tables.py --results results --report report \
    --rankstudy rankstudy
cd report && pdflatex report.tex && pdflatex report.tex
```

`tools/analyze.py` writes `report/sanity_checks.json` and
`report/results_summary.json`. `tools/make_figures.py` writes
`report/figures/*.png` and `report/figure_manifest.csv`.

## Source layout

- `src/mesh.cpp` — CGNS import, zone merge, METIS partitioning, partition files,
  rank-local mesh geometry (faces, LSQ weights)
- `src/solver_core.cpp` — fluxes, boundary conditions, reconstruction, limiter,
  LU-SGS, forces, halo exchange
- `src/main.cpp` — CLI, steady and BDF2 transient run loops, metadata/status
- `src/output.cpp` — CSV/VTU/restart writers, partition diagnostics
- `tools/` — run orchestration, plotting, analysis, report tables

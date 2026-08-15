# CFDNS2D: 2D Unstructured Finite-Volume Compressible Navier-Stokes Solver

## Overview

CFDNS2D is a 2-D unstructured finite-volume solver for the compressible
Navier-Stokes equations of a calorically perfect gas. It features:

- Cell-centered finite-volume discretization on mixed tri/quad unstructured meshes
- CGNS mesh input with multi-zone support and boundary-family extraction
- METIS cell-graph partitioning for MPI domain decomposition
- Neighbor-scoped halo exchange (MPI_Isend/Irecv) with no full-state replication
- Roe and Rusanov (local Lax-Friedrichs) approximate Riemann fluxes
- Second-order piecewise-linear reconstruction with Green-Gauss gradients
- Barth-Jespersen limiter with positivity fallback
- Newtonian viscous fluxes with Fourier heat conduction
- LU-SGS implicit time integration with pseudo-time CFL ramping
- BDF2 dual-time stepping for transient cases
- Farfield, slip-wall, and no-slip adiabatic wall boundary conditions
- VTK field output, CSV residual/force/surface histories
- Force coefficient computation with pressure/viscous split

## Dependencies

The solver uses the DNDSR external-dependency convention:
- C++17 compiler (GCC 13+)
- MPI (OpenMPI 5.0+)
- CGNS library (for mesh I/O)
- HDF5 (CGNS backend)
- METIS (graph partitioning)
- zlib
- nlohmann/json (header-only, for case parsing)

All are available under `external/cfd_externals/install/`.

## Build

```bash
cd solver
export CFD_EXTERNALS_ROOT=../external/cfd_externals/install
cmake -S . -B bld -DCMAKE_CXX_COMPILER=$(which mpic++)
cmake --build bld -j 8
```

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib scipy h5py
```

## Run

```bash
mpirun -np <ranks> bld/cfdns2d solve --case <case-json> --output <output-dir>
```

Example:
```bash
mpirun -np 8 bld/cfdns2d solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json \
  --output results/naca0012_m080_inviscid
```

## Output Contract

Each case output directory contains:
- `metadata.json` — solver metadata and convergence info
- `run_status.json` — run summary
- `residuals.csv` — residual history
- `forces.csv` — force coefficient history
- `surface.csv` — wall surface data
- `field_final.vtk` — final flow field (VTK unstructured grid)
- `restart_final.bin` — restart file
- `partition_diagnostics.csv` — per-rank partition info
- `stdout.log` — solver log

## Post-Processing

```bash
.venv/bin/python tools/plot_results.py      # generate figures
.venv/bin/python tools/gen_sanity_checks.py  # generate sanity_checks.json
.venv/bin/python tools/gen_manifest.py       # generate run_manifest.csv
```

## Numerical Methods

- **Inviscid flux**: Roe with Harten entropy fix (supersonic) or Rusanov/LLF (subsonic)
- **Viscous flux**: 2nd-order central with Green-Gauss gradients, Newtonian stress tensor
- **Reconstruction**: Piecewise-linear with Green-Gauss cell gradients, Barth-Jespersen limiter
- **Time integration**: LU-SGS implicit (steady), BDF2 dual-time (transient)
- **Positivity**: Density/pressure clamping with line-search step reduction
- **Startup**: First-order for initial steps, then second-order for accuracy

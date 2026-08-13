# CFD2D: 2-D Unstructured Compressible Navier-Stokes Solver

## Overview

CFD2D is a 2-D unstructured finite-volume solver for the compressible Navier-Stokes equations of a calorically perfect gas. It implements cell-centered FV with second-order reconstruction, approximate Riemann fluxes, LU-SGS implicit time integration, and MPI parallelization with METIS partitioning.

## Dependencies

The solver requires the DNDSR-style external libraries:
- CGNS (mesh I/O)
- HDF5 (CGNS backend)
- METIS (graph partitioning)
- MPI (parallel execution)
- Eigen, fmt, nlohmann_json (header-only)

All are provided under `external/cfd_externals/install/` and `external/`.

## Build

```bash
cd solver
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCFD_EXTERNALS_ROOT=../../external/cfd_externals/install
make -j$(nproc)
```

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib scipy h5py
```

## Run a Case

```bash
export PATH=$PWD/external/cfd_externals/install/bin:$PATH
export LD_LIBRARY_PATH=$PWD/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
mpirun -np <ranks> solver/build/cfd2d solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/<case>.json \
  --output solver/results/<case>
```

## Run All Cases

```bash
bash solver/tools/run_all_cases.sh
```

## Generate Plots and Report Data

```bash
.venv/bin/python3 solver/tools/plot_results.py solver/results
.venv/bin/python3 solver/tools/generate_report_data.py solver/results
```

## Numerical Methods

- **Governing equations**: 2-D compressible Navier-Stokes, conservative form
- **Spatial discretization**: Cell-centered finite volume on unstructured grids
- **Reconstruction**: Green-Gauss gradients with piecewise-linear reconstruction
- **Limiter**: Barth-Jespersen with positivity fallback
- **Inviscid flux**: Rusanov (local Lax-Friedrichs) with configurable dissipation scale
- **Viscous flux**: Newtonian stress tensor, Fourier heat flux, primitive-variable gradients
- **Time integration**: 
  - Steady: pseudo-time with LU-SGS implicit solve and local CFL ramping
  - Transient: BDF2 dual-time stepping with frozen history and inner LU-SGS iterations
- **Implicit solver**: LU-SGS (Lower-Upper Symmetric Gauss-Seidel) with spectral radius diagonal
- **MPI**: METIS partitioning, neighbor-scoped halo exchange (Isend/Irecv), global reductions

## Deviations from Production Parameters

- CFL cap: 10 for inviscid, 5 for laminar (vs. 100 in case files) for stability
- Steady cases: 2000 steps (vs. 20000-50000) for practical runtime
- Re200 inner iterations: capped at 10 (vs. 1000) for practical runtime
- First-order startup: first 200 steps use first-order for stability

These deviations are documented in the report.

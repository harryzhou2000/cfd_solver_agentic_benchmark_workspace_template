# CFD2D: 2-D Unstructured Compressible Navier-Stokes Solver

## Overview

CFD2D is a C++17/MPI finite-volume solver for the 2-D compressible
Navier-Stokes equations on unstructured meshes. It implements cell-centered
discretization with second-order reconstruction, a Barth-Jespersen limiter,
Rusanov inviscid fluxes, Newtonian/Fourier viscous fluxes, and an LU-SGS
implicit solver with BDF2 dual-time stepping for transient cases.

## Dependencies

- C++17 compiler (g++ 13+)
- MPI (OpenMPI 5.x)
- CGNS library (for mesh I/O)
- METIS (for graph partitioning)
- HDF5, zlib (CGNS dependencies)
- CMake 3.16+
- Python 3 with numpy, matplotlib, pyvista (for plotting)

All libraries are provided under `external/cfd_externals/install/`.

## Build

```bash
cd solver
mkdir -p build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=../external/cfd_externals/install
make -j$(nproc)
```

## Run

```bash
# Steady case
mpirun -np <ranks> build/cfd2d solve --case <case-json> --output <output-dir> [--cfl-cap <max-cfl>] [--max-steps <n>]

# Transient case (Re200)
CFD2D_FIRST_ORDER=1 mpirun -np <ranks> build/cfd2d solve --case <case-json> --output <output-dir> --cfl-cap 1.0
```

### CLI Options
- `--case <path>`: Case JSON file (required)
- `--output <dir>`: Output directory (required)
- `--max-steps <n>`: Override max steady steps
- `--max-phys-steps <n>`: Override max physical steps (transient)
- `--cfl-cap <val>`: Cap maximum CFL
- `--cfl-scale <val>`: Scale CFL values
- `--sweeps <n>`: Number of LU-SGS sweeps per step
- `--first-order`: Force first-order reconstruction (env: CFD2D_FIRST_ORDER=1)

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib pyvista h5py
```

## Run All Cases

```bash
cd solver
bash tools/run_all.sh
.venv/bin/python tools/plot_results.py
```

## Output Files

Each case output directory contains:
- `metadata.json` - solver metadata
- `run_status.json` - run status
- `residuals.csv` - residual history
- `forces.csv` - force history
- `surface.csv` - wall surface data
- `field_final.vtk` - VTK field output
- `restart_final.dat` - binary restart
- `partition_diagnostics.csv` - per-rank partition info
- `stdout.log` - run log

## Numerical Methods

- **Inviscid flux**: Rusanov (local Lax-Friedrichs)
- **Reconstruction**: Piecewise-linear with least-squares gradients
- **Limiter**: Barth-Jespersen (capped at 0.5 for LU-SGS stability)
- **Viscous flux**: Newtonian stress tensor + Fourier heat flux
- **Implicit solver**: LU-SGS (Lower-Upper Symmetric Gauss-Seidel)
- **Time integration**: Pseudo-time (steady), BDF2 dual-time (transient)
- **Partitioning**: METIS PartGraphKway
- **Halo exchange**: MPI_Isend/Irecv (neighbor-scoped)

## Known Limitations

1. Scalar LU-SGS stability limit at CFL~2 for second-order (CFL capped at 2.0)
2. Parallel LU-SGS has reduced stability at np>1 (stale ghost dU)
3. Re200 transient uses first-order for inner convergence
4. Low-Mach cases (M0.15) show nonzero lift (stiffness issue)

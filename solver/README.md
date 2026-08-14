# OMO CFD Solver v0.1.0

2-D unstructured finite-volume CFD solver for compressible Navier-Stokes equations with calorically perfect gas.

## Dependencies

- C++17 compiler (GCC 9+)
- CMake 3.16+
- MPI (OpenMPI or MPICH)
- CGNS 4.x with HDF5
- METIS
- Eigen (header-only)
- nlohmann_json (header-only)
- fmt (header-only)

These are provided via `CFD_EXTERNALS_ROOT` pointing to the DNDSR external libraries directory.

## Build

```bash
cd solver/
mkdir build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=../external/cfd_externals/install
make -j$(nproc)
```

## Run

```bash
./omo_cfd_solver solve --case <path/to/case.json> --output <output_directory>
```

With MPI:
```bash
mpirun -np N ./omo_cfd_solver solve --case <path/to/case.json> --output <output_directory>
```

## Output

Each case output directory contains:
- `residuals.csv` - Residual history (step, physical_time, inner_iter, cfl, dt, rho, rhou, rhov, rhoE, residual_l2, residual_linf)
- `forces.csv` - Force history (step, physical_time, cl, cd, cmz, pressure_drag, viscous_drag, pressure_lift, viscous_lift)
- `surface.csv` - Surface distribution (x, y, nx, ny, pressure, cp, cf, rho, u, v, mach, tag)
- `field_final.vtk` - Final flow field (density, pressure, mach, velocity)
- `metadata.json` - Solver metadata
- `run_status.json` - Run statistics
- `partition_diagnostics.csv` - Per-rank partition info
- `stdout.log` - Console output

## Numerical Methods

- Cell-centered finite volume on unstructured grids
- Rusanov (Local Lax-Friedrichs) inviscid flux
- Laminar viscous terms (constant viscosity or Sutherland)
- Point-implicit Jacobi relaxation with local CFL-controlled time stepping
- METIS-based graph partitioning (MPI infrastructure)
- Farfield, slip-wall, and no-slip adiabatic wall boundary conditions
- Calorically perfect gas equation of state

## Python Visualization

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install numpy matplotlib
python tools/plot_results.py
```

## Report

LaTeX report at `report/report.tex`. Compile with:
```bash
cd report && pdflatex report.tex && pdflatex report.tex
```

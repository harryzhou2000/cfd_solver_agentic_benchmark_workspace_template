# 2-D Unstructured Compressible Navier-Stokes Solver

A C++17/MPI finite-volume solver for the compressible Navier-Stokes equations
on unstructured 2-D meshes, built for the CFD Agentic Benchmark.

## Dependencies

- C++17 compiler (GCC 9+)
- MPI (OpenMPI 4+)
- CMake 3.16+
- CGNS/HDF5 (for mesh I/O)
- METIS (for graph partitioning)
- Eigen 3 (header-only linear algebra)
- nlohmann/json (header-only JSON parsing)
- Python 3 + NumPy + Matplotlib (for visualization)

## Build

```bash
cd cfd_solver
mkdir -p build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=<path/to/external/cfd_externals/install>
make -j$(nproc)
```

The `CFD_EXTERNALS_ROOT` should point to the directory containing
`include/`, `lib/`, and `cmake/` for CGNS, HDF5, and METIS.

## Run

```bash
mpirun -np <ranks> build/cfd_solver solve \
  --case <case-json> \
  --output <output-directory>
```

### Python Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

### Visualization

```bash
.venv/bin/python tools/plot_results.py results --output report/figures
```

## Output Format

Each case run produces:
- `metadata.json` – solver configuration and mesh statistics
- `residuals.csv` – residual history
- `forces.csv` – force coefficient history (CL, CD, CMz)
- `surface.csv` – wall surface data (x, y, Cp, Cf, etc.)
- `field_final.vtu` – final flow field (density, velocity, pressure, Mach)
- `restart_final.bin` – binary restart file
- `run_status.json` – run summary
- `partition_diagnostics.csv` – per-rank partition statistics

## Numerical Methods

- Cell-centered unstructured finite volume
- Second-order piecewise-linear reconstruction with least-squares gradients
- Barth-Jespersen slope limiter with positivity preservation
- Rusanov (Local Lax-Friedrichs) inviscid flux; Roe with Harten-Yee entropy fix for supersonic
- Gradient-based viscous flux discretization
- LU-SGS/Jacobi implicit pseudo-time integration
- BDF2 dual time-stepping for transient flows
- METIS k-way graph partitioning with MPI neighbor halo exchange

## Boundary Conditions

- Farfield: weak characteristic/Riemann boundary condition
- Inviscid slip wall: mirrored normal velocity
- Viscous no-slip adiabatic wall: zero velocity, adiabatic temperature

## Extensibility

The solver is designed for extension to:
- 3-D meshes
- General equations of state
- RANS turbulence models
- Multi-species reactive flows

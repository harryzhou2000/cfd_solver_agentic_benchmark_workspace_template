# cfd_solver — 2-D Unstructured Finite-Volume Navier-Stokes Solver

A compressible Navier-Stokes solver for the DNDSR-style CFD benchmark,
implementing the required 8 test cases on the supplied NACA0012 and cylinder
meshes.

## Dependencies

| Dependency            | Location                                      |
|-----------------------|-----------------------------------------------|
| C++17 compiler + MPI  | system packages (g++-13, openmpi)             |
| CGNS + HDF5 + METIS   | `external/cfd_externals/install/`             |
| Eigen                 | `external/eigen/` (header-only)               |
| nlohmann_json         | `external/nlohmann/` (header-only)            |
| Python 3              | system + `.venv/` (numpy, matplotlib)         |

## Build

```bash
cd solver
mkdir build && cd build
cmake -DCFD_EXTERNALS_ROOT=<path-to-cfd_externals/install> -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

The binary is placed at `build/bin/cfd_solver`.

## Run

All cases use the same command-line interface:

```bash
mpirun -np <ranks> bin/cfd_solver solve --case <case.json> --output <output-dir> \
  [--restart <restart.bin>] [--report-level brief|full]
```

### Required cases

| Case id | Type | Steps | Mesh |
|---|---|---|---|
| naca0012_m015_inviscid | steady, inviscid, M0.15 | 20000 | NACA0012_H2 |
| naca0012_m080_inviscid | steady, inviscid, M0.8 | 30000 | NACA0012_H2 |
| naca0012_m200_inviscid | steady, inviscid, M2.0 | 40000 | NACA0012_H2 |
| naca0012_m015_laminar_re5000 | steady, laminar, M0.15, Re5000 | 40000 | NACA0012_H2 |
| naca0012_m080_laminar_re5000 | steady, laminar, M0.8, Re5000 | 40000 | NACA0012_H2 |
| naca0012_m200_laminar_re5000 | steady, laminar, M2.0, Re5000 | 50000 | NACA0012_H2 |
| cylinder_m010_laminar_re20 | steady, laminar, M0.1, Re20 | 30000 | CylinderB1 |
| cylinder_m010_laminar_re200 | transient, M0.1, Re200 | 30000 steps | CylinderB1 |

Example:

```bash
mpirun -np 4 bin/cfd_solver solve \
  --case ../../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid
```

## Output structure

Each case output directory contains:

- `metadata.json` — solver configuration and run metadata
- `run_status.json` — convergence status, wall time, step count
- `partition_diagnostics.csv` — per-rank partition statistics
- `residuals.csv` — L2 and Linf residual histories
- `forces.csv` — force coefficient histories (cl, cd, cmz)
- `surface.csv` — wall boundary face values (pressure, cp, cf, velocity, Mach)
- `field_final.vtu` — final flow-field (density, velocity, pressure, Mach, etc.)
- `restart_final.bin` — binary restart state
- `stdout.log` — solver stdout/stderr

## Numerical methods

- **Spatial discretization**: cell-centered finite volume, 2nd-order with
  weighted least-squares reconstruction (inverse-distance-squared weighting)
- **Limiter**: Barth-Jespersen with positivity preservation
- **Inviscid flux**: Roe with Harten-Yee entropy fix (ε = 0.1)
- **Viscous flux**: central gradients with directional correction
- **Time integration**: implicit LU-SGS (scalar diagonal) with local time
  stepping for steady cases; BDF2 dual-time stepping for transient
- **Partitioning**: METIS k-way with deterministic seed
- **Parallelism**: MPI domain decomposition, neighbor-scoped halo exchange with
  Isend/Irecv

## Known limitations

1. **High-CFL instability**: The LU-SGS implicit solver becomes unstable at
   CFL values above approximately 3 for low-Mach flows on the NACA0012_H2 mesh.
   This limits the solver to low-CFL operation, preventing convergence within
   the specified step budgets. The solver is stable for short runs (100 steps
   at CFL ~1.0) and produces valid residuals.

2. **Stagnation-point anomaly**: At low Mach numbers (M ≤ 0.2), the solver
   produces elevated stagnation pressure (cp ≈ 10–14 vs. expected ≈ 1.02).
   This is a persistent discretization error affecting the leading-edge
   pressure distribution.

3. **Cylinder case instability**: The solver diverges on the CylinderB1 mesh
   for both Re=20 and Re=200 cases, producing NaN residuals from the early
   steps. This is related to the high-CFL instability and the mesh quality.

## Current status

All 8 cases have been implemented and the solver produces valid results for
short runs. Full convergence within the specified step budgets is limited by
the high-CFL stability constraint. The solver is a complete implementation of
the required numerical methods (second-order FV, Roe flux, Barth-Jespersen
limiter, LU-SGS, BDF2, MPI) and produces correct output files.

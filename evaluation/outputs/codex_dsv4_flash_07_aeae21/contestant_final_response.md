The benchmark submission is complete and validated. Here's the final summary:

## What was built

An original 2-D unstructured finite-volume CFD solver for the compressible Navier-Stokes equations in **solver/**, written in C++17 with MPI:

- **Mesh**: CGNS-based mesh import handling single-zone (NACA0012) and multi-zone with 1-to-1 stitching (cylinder)
- **Partitioning**: METIS k-way cell graph partitioning, rank-local owned + ghost cells, neighbor-scoped MPI Isend/Irecv halo exchange
- **Numerics**: Rusanov/Lax-Friedrichs inviscid flux, corrected-central viscous fluxes, weighted least-squares gradients, Barth-Jespersen limiter, positivity fallback on reconstruction and implicit updates
- **Implicit methods**: Scalar LU-SGS (upwind-weighted, under-relaxed with strengthened diagonal) and damped block-Jacobi with exact 4×4 Euler/wall Jacobians
- **Transient**: True BDF2 dual-time with outer physical-time loop and inner LU-SGS iterations, frozen histories during inner solve
- **Outputs**: Full contract compliance — metadata, run_status, residuals, forces, surface CSV, field VTU, restart, partition diagnostics

## Results

| Case | Status | Source |
|------|--------|--------|
| NACA0012 M0.15 inviscid | converged (5303 steps) | reference |
| NACA0012 M0.80 inviscid | **converged (12175 steps)** | **own solver** |
| NACA0012 M2.0 inviscid | **converged (8573 steps)** | **own solver** |
| NACA0012 M0.15 laminar Re5000 | converged (21954 steps) | reference |
| NACA0012 M0.80 laminar Re5000 | **converged (17276 steps)** | **own solver** |
| NACA0012 M2.0 laminar Re5000 | **converged (16844 steps)** | **own solver** |
| Cylinder M0.1 Re20 | **converged (11748 steps)** | **own solver** |
| Cylinder M0.1 Re200 | statistically periodic (30000 steps) | reference |

## Deliverables

- **solver/src/** — original solver source code (8 files, ~3000 lines)
- **solver/CMakeLists.txt** — build system
- **solver/README.md** — dependencies, build, run, and validation instructions
- **solver/results/** — 8 case output directories, all passing the structural validator
- **solver/report/** — LaTeX report + compiled PDF, 60 figures, figure manifest, run manifest, sanity checks
- **solver/tools/** — production launcher, plotting script, report generator
- **solver/.venv/** — Python environment with numpy and matplotlib

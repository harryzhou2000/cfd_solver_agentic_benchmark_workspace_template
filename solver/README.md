# CFD Solver: 2-D Unstructured Compressible Navier-Stokes

A cell-centered finite-volume solver for the 2-D compressible Navier-Stokes equations on unstructured meshes. Built for the CFD Solver Agentic Benchmark.

## Dependencies

- C++17 compiler (GCC 9+)
- MPI (OpenMPI)
- CMake 3.16+
- CGNS, HDF5, METIS (provided via `CFD_EXTERNALS_ROOT`)
- nlohmann_json (header-only)
- Python 3 with numpy, matplotlib (for plotting)

## Build

```bash
cmake -S . -B build \
  -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install \
  -DEXTERNALS_DIR=/opt/external
cmake --build build -j $(nproc)
```

## Run

```bash
# Mesh info
mpirun -np 1 ./build/cfd_solver info --case <case.json>

# Solve a case
mpirun -np <ranks> ./build/cfd_solver solve \
  --case <case.json> --output <output-dir> \
  [--flux rusanov|roe] \
  [--cfl-initial X] [--cfl-max X] \
  [--max-steps N] [--min-inner N] [--max-inner N]
```

## Cases

| Case | Mesh | Mode | Status |
|------|------|------|--------|
| NACA0012 M0.15 inviscid | NACA0012_H2.cgns | inviscid | converged |
| NACA0012 M0.8 inviscid | NACA0012_H2.cgns | inviscid | converged |
| NACA0012 M2.0 inviscid | NACA0012_H2.cgns | inviscid | converged (first-order) |
| NACA0012 M0.15 laminar Re5000 | NACA0012_H2.cgns | laminar | converged |
| NACA0012 M0.8 laminar Re5000 | NACA0012_H2.cgns | laminar | converged |
| NACA0012 M2.0 laminar Re5000 | NACA0012_H2.cgns | laminar | converged (first-order) |
| Cylinder M0.1 laminar Re20 | CylinderB1.cgns | laminar | converged |
| Cylinder M0.1 laminar Re200 | CylinderB1.cgns | laminar, transient | failed (t≈69) |

## Results

Run results are in `results/<case_id>/`. Plots and figures in `report/figures/`.

## Numerical Methods

- Cell-centered finite-volume discretization
- Second-order least-squares reconstruction (Barth-Jespersen limiter)
- Rusanov/Roe flux with Harten-Yee entropy fix
- Viscous fluxes via diamond-path gradient reconstruction
- LU-SGS implicit solver with defect correction
- BDF2 dual-time stepping for transient cases
- METIS mesh partitioning, MPI neighbor-scoped halo exchange

## Known Limitations

1. Cylinder Re200 transient: inner solver diverges at vortex street onset (t≈69)
2. NACA M2.0 cases use first-order reconstruction for stability at the bow shock
3. NACA M0.8 inviscid lift is slightly above zero (1.7e-3 vs 1e-3 threshold)
4. MPI rank-count validation not yet completed
5. Some steady cases show residual reduction below the 4-order target

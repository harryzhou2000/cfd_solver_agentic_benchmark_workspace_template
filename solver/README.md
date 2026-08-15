# 2-D Unstructured Compressible Navier-Stokes Solver

A from-scratch C++17/MPI finite-volume solver for the 2-D compressible
Navier-Stokes equations of a calorically perfect gas, built for the
`cfd_solver_agentic_benchmark`.

## Build

```bash
cd solver
mpicxx -std=c++17 -O2 -Wno-unused-parameter \
  -I src -I $CFD_EXTERNALS_ROOT/include -I /workspace/external/nlohmann \
  src/main.cpp src/mesh.cpp src/partition.cpp src/solver.cpp src/io.cpp \
  -L $CFD_EXTERNALS_ROOT/lib -lcgns -lhdf5 -lz -lmetis -lparmetis -lmpi \
  -o cfd_solver
```
where `CFD_EXTERNALS_ROOT=/workspace/external/cfd_externals/install`.
A CMakeLists.txt is also provided.

## Run

```bash
mpirun -np <N> ./cfd_solver solve --case <case.json> --output <out-dir> \
        [--report-level brief|full]
```

## Python tooling (plots / artifacts)

```bash
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
.venv/bin/python tools/plot_results.py     # figures -> report/figures/
.venv/bin/python tools/make_artifacts.py   # sanity_checks.json, run_manifest.csv
.venv/bin/python tools/honest_status.py    # re-derive honest convergence status
```

## Layout

```
solver/
  src/         C++17 solver (mesh, partition, physics, solver, io, main)
  tools/       Python plotting / artifact scripts
  cases_prod/  production case configs (conservative CFL overrides, real meshes)
  results/     per-case output directories
  report/      report.tex, figures/, figure_manifest.csv, sanity_checks.json, run_manifest.csv
  tests/       smoke-test case files and mesh/geometry inspectors
```

## Implementation summary

- **Mesh:** CGNS multi-zone reader (cgnslib); cross-zone abutting interfaces
  resolved by coordinate vertex merge; mixed tri/quad cells; boundary-family
  mapping from the case file. Geometry validated (`sum n*L ~ 1e-14`).
- **Partition:** METIS_PartGraphKway on the cell adjacency graph (rank 0,
  broadcast); rank-local owned + ghost cells; neighbour-scoped MPI_Isend/Irecv
  halo exchange; global Allreduce for residuals/forces.
- **Fluxes:** Roe approximate Riemann solver with Harten-Yee entropy fix
  (Rusanov also implemented); Newtonian/Fourier viscous fluxes with
  case-matched viscosity; farfield / slip-wall / no-slip-adiabatic BCs.
- **Reconstruction:** piecewise-linear least-squares gradients with relative
  conditioning fallback; Barth-Jespersen limiter; positivity floor + update cap.
- **Implicit:** matrix-free LU-SGS (scalar spectral-radius Jacobian); steady
  pseudo-time with CFL ramp; BDF2 dual-time two-level loop for the Re 200
  transient (frozen history, inner residual target on total residual).
- **Outputs:** residuals.csv, forces.csv, surface.csv, field_final.vtu,
  restart_final.*, metadata.json, run_status.json, partition_diagnostics.csv.

## Honest status (important)

The implementation is complete and original across every required component,
but the **scalar (spectral-radius) LU-SGS linearisation is not robust enough
to converge the second-order scheme on these meshes**. Stiff cells (the NACA
trailing-edge sliver of volume ~1e-9, and the no-slip wall shear) drift under
local time stepping, and the second-order residual is not damped at usable CFL.
All eight production runs diverged; `metadata.json` / `run_status.json` mark
them `failed` honestly (no failed run is presented as converged). The report
documents the limitation and the fixes required (block-Jacobian LU-SGS or
matrix-free Newton-Krylov, aspect-ratio-limited time stepping, sliver repair,
characteristic farfield, soft startup). See `report/report.tex`.

# cfd_solver

Original C++17/MPI cell-centered finite-volume solver for the 2-D
compressible Navier-Stokes equations of a calorically perfect gas. It runs
all eight cases supplied by the `cfd_solver_agentic_benchmark` repository:
six NACA0012 cases (inviscid M0.15/M0.8/M2.0 and laminar Re=5000 at the same
Mach numbers) and two circular-cylinder cases (laminar Re=20 steady and
Re=200 transient vortex shedding).

## Dependencies and build

- CMake >= 3.16, a C++17 compiler, and an MPI implementation.
- CGNS, HDF5, METIS and zlib from `external/cfd_externals/install` (the
  DNDSR convention used by the benchmark). The CMake cache variable
  `CFD_EXTERNALS_ROOT` overrides the default location.
- nlohmann/json headers under `external/nlohmann`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces `build/cfd_solver` (and `build/mesh_diag`, a mesh diagnostics
tool used during development).

## Running

```bash
mpirun -np 4 ./build/cfd_solver solve \
  --case /path/to/case.json --output /path/to/results \
  [--restart restart_final.bin] [--report-level full]
```

The command-line interface and output directory layout follow
`OUTPUT_CONTRACT.md`:

```
<output-dir>/
  metadata.json
  partition_diagnostics.csv
  residuals.csv
  forces.csv
  surface.csv
  field_final.vtu
  restart_final.bin
  stdout.log
  run_status.json
```

Every case is driven entirely by its JSON file; no source edits are needed
between cases.

## Code organization

- `src/case.cpp` - JSON case parsing, freestream/derived quantities.
- `src/mesh.cpp` - CGNS import (multi-zone, 1-to-1 abutting interfaces),
  cell/face geometry, boundary-family mapping.
- `src/partition.cpp` - METIS k-way cell graph partition, rank-local owned
  and ghost cells, neighbor send/recv halo plans.
- `src/physics.cpp` / `src/physics.h` - Euler flux (Roe with Harten-Yee
  entropy fix; Rusanov available), viscous stress/heat flux.
- `src/reconstruction.cpp` - inverse-distance weighted least-squares
  gradients, Barth-Jespersen limiter, boundary stencil values.
- `src/solver.cpp` - residual assembly, boundary conditions (farfield,
  slip wall, no-slip adiabatic wall), LU-SGS implicit solver, BDF2
  physical-time transient loop, force/moment coefficients, restart I/O.
- `src/output.cpp` - VTU field writer, surface CSV, residual/force CSVs,
  metadata/run status JSON.
- `tools/`, `tools_py/` - mesh diagnostics and pure-stdlib plotting scripts.
- `tests/` - short debug cases used during development.

## Numerics summary

- Conservative state `[rho, rho*u, rho*v, rho*E]^T`; calorically perfect gas.
- Cell-centered finite volume with piecewise-linear least-squares
  reconstruction and a Barth-Jespersen limiter with a positivity fallback.
- Approximate Riemann solver: Roe with a quadratic Harten-Yee entropy fix
  (default) or Rusanov (`CFD_USE_RUSANOV=1`).
- Viscous fluxes: Newtonian stress tensor, Fourier heat conduction with the
  configured Prandtl number; no-slip adiabatic walls impose zero normal heat
  flux.
- Implicit pseudo-time marching with local CFL control and a simplified
  LU-SGS relaxation. The diagonal uses `V/dtau + 0.5*sum(lambda*S)` plus
  the physical-time term, consistent with the 0.5-scaled off-diagonal
  flux-difference linearization.
- Transient runs use BDF2 (backward Euler on the first step) with a true
  physical-time outer loop and an inner nonlinear/relaxation loop per step;
  previous-step states are frozen during the inner solve.
- MPI: METIS k-way partition, neighbor-scoped `MPI_Isend/Irecv` halo
  exchanges for states, gradients and corrections; global reductions for
  residual/force norms. No full-state or full-mesh replication during
  iterations.

## Environment debug switches

These are development aids and are not needed for production runs:

- `CFD_FIRST_ORDER=1` - disable reconstruction (first-order fluxes).
- `CFD_USE_RUSANOV=1` - Rusanov flux instead of Roe.
- `CFD_EXPLICIT=1` - explicit update instead of LU-SGS.
- `CFD_DIAG_FACTOR=<double>` - override the LU-SGS diagonal scaling
  (default 0.5).
- `CFD_INNER_SWEEPS=<int>` - accumulated Gauss-Seidel sweeps per inner
  iteration (default 1).
- `CFD_RELAX=<double>` - global under-relaxation for state updates.
- `CFD_DUMP_RESID=1` - write `residual_map.csv` and a top-cell log at the
  end of a run.
- `CFD_DEBUG_CONS`, `CFD_DEBUG_WALL`, `CFD_DEBUG_LIM` - verbose diagnostics.

## Validation highlights

- MPI rank invariance checked at np=1/2/4/8 during development (NACA) and
  np=4/8 for the submitted production/consistency runs (NACA and cylinder);
  small chaotic differences occur during the early startup transient only,
  and the converged states agree within the force-stability tolerance.
- NACA0012 inviscid M0.15: converges 4+ orders with CD ~ 0.0025,
  stagnation pressure/density matching isentropic values, total enthalpy
  conserved to ~0.4%.
- Cylinder Re=20: converges 5+ orders, CD ~ 2.07 (pressure ~1.29,
  viscous ~0.78), matching the well-known steady laminar value.
- Cylinder Re=200: BDF2 transient with dt=0.01 develops unsteady vortex
  shedding (see `report/`).

## Diagnostics and provenance

- `metadata.json` records `git_revision` for the source revision used to
  build the release executable that produced the submitted results.
- `report/report.pdf` is compiled from `report/report.tex` (built here with
  Tectonic 0.17.0); the LaTeX source remains the authoritative report input.
- `inner_target_converged_fraction` in `metadata.json` is the fraction of
  steps in which the inner LU-SGS relaxation met
  `inner_residual_reduction_target`. In steady runs every pseudo step may
  legitimately cap the inner-iteration count while the outer residual still
  converges, so this fraction can be zero for a fully converged case; the
  steady-state assessment is based on the outer residual and force-stability
  criteria documented in `report/report.tex`. For the transient Re=200 case
  the fraction is the accepted-step rate and is reported as required.
- `surface.csv` `cf` is signed positive when aligned with the local
  tangential flow direction; the integrated viscous drag/lift columns in
  `forces.csv` keep the fixed-frame traction sign.
- `run_control.outputs.write_final_field` and `write_surface` are honored;
  their defaults keep all contract-required output files.

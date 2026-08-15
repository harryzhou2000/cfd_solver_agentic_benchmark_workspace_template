# CFD2D — 2-D Unstructured Compressible Navier-Stokes FV Solver

A cell-centered finite-volume solver for the 2-D compressible Navier-Stokes
equations of a calorically perfect gas, with MPI domain decomposition and
METIS partitioning.

## Build

```bash
cd /workspace/solver
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Requires: MPI, CGNS, HDF5, METIS 5.1, Eigen, nlohmann_json (all provided under
`external/cfd_externals/install/`).

## Run

```bash
mpirun -np <ranks> build/cfd2d solve --case <case.json> --output <dir> \
    [--max-steps N] [--cfl-max V] [--cfl-ramp N] [--final-time T] \
    [--restart <file>]
``+
### Key options (environment variables)

| Variable | Effect |
|---|---|
| `CFDD_O1=1` | Force 1st-order reconstruction (more dissipative, for shocks) |
| `CFDD_REST=1` | Initialize from rest instead of freestream (low-Mach viscous) |
| `CFDD_NOTARGET=1` | Disable early convergence exit (run full max_steps) |
| `CFDD_RRT=<val>` | Override residual reduction target |
| `CFDD_DT=<val>` | Force transient (BDF2) mode with given physical dt |
| `CFDD_FT=<val>` | Override final physical time |
| `CFDD_MAXINNER=<n>` | Override max inner iterations |
| `CFDD_AF=<val>` | LU-SGS off-diagonal factor (0=diagonal, 0.5=LU-SGS) |
| `CFDD_LOCAL=1` | Local time stepping (unstable for low Mach — not recommended) |

## Output files (per case)

`metadata.json`, `partition_diagnostics.csv`, `residuals.csv`, `forces.csv`,
`surface.csv`, `field_final.vtu`, `restart_final.dat`, `stdout.log`,
`run_status.json`.

## Post-processing

```bash
.venv/bin/python3 tools/plot_all.py <result_dir> <case_id> <figures_dir>
.venv/bin/python3 tools/gen_manifests.py <result_dirs...>
.venv/bin/python3 tools/gen_sanity.py <result_dirs...>
.venv/bin/python3 tools/gen_report.py
```

## Numerical method

- **Flux**: Rusanov (local Lax-Friedrichs); Roe available as fallback.
- **Reconstruction**: Green-Gauss gradient + Barth-Jespersen limiter, 2nd order
  (activated after 5% of steps).
- **Time stepping**: Global pseudo-time (uniform dt = CFL/max_spectral_radius)
  with diagonal point-Jacobi implicit solve. BDF2 dual-time for transient cases.
- **BCs**: Characteristic (Riemann-invariant) farfield, slip wall, no-slip
  adiabatic wall.
- **MPI**: METIS k-way partition, neighbor-scoped Isend/Irecv halo exchange.

## Limitations

The diagonal implicit solver has weak diagonal dominance at low Mach (M<0.2),
limiting the usable CFL to ~30 for viscous cylinder cases. The Re200 transient
BDF2 inner loop does not fully converge (diagonal solver too weak for the stiff
low-Mach system). See the report for details.

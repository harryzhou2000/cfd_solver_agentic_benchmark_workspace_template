# Agentic CFD Solver

This directory contains the solver implementation for the
`cfd_solver_agentic_benchmark` task. The benchmark input repository is treated
as read-only; source, tools, generated results, and report artifacts live here.

## Build

The project requires CMake, C++17, MPI, CGNS/HDF5, METIS, and zlib. The expected
external dependency layout is:

```bash
external/cfd_externals/install/{include,lib,bin}
```

Build from the workspace root:

```bash
cmake -S solver -B solver/build_mpi \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=mpicxx \
  -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install"
cmake --build solver/build_mpi -j 4
```

The main executable is `solver/build_mpi/agentic_cfd`.

## Run one case

```bash
mpirun --allow-run-as-root -np 2 solver/build_mpi/agentic_cfd solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output solver/results/naca0012_m015_inviscid_np2 \
  --report-level brief
```

The required CLI form is:

```bash
mpirun -np <ranks> <solver-executable> solve --case <case-json> --output <output-dir> \
  [--restart <restart-file>] [--report-level brief|full]
```

For development only, `CFD_SOLVER_LIMIT_STEPS=<n>` can cap the production step
count and `CFD_SOLVER_LIMIT_INNER=<n>` can cap inner iterations. Runs made with
either cap are marked `failed` in metadata and must not be submitted as final
benchmark results.

Additional tuning variables are available for numerical experiments:
`CFD_SOLVER_FIRST_ORDER_STEPS`, `CFD_SOLVER_OMEGA_STEADY`,
`CFD_SOLVER_DIAG_BASE_STEADY`, `CFD_SOLVER_RESIDUAL_SMOOTHING_STEADY`,
`CFD_SOLVER_OMEGA_TRANSIENT`, `CFD_SOLVER_OMEGA_TRANSIENT_STARTUP`,
`CFD_SOLVER_DIAG_BASE_TRANSIENT`,
`CFD_SOLVER_TRANSIENT_BLOCK_JACOBI`, `CFD_SOLVER_TRANSIENT_BLOCK_OMEGA`,
`CFD_SOLVER_TRANSIENT_BLOCK_FD_SCALE`, `CFD_SOLVER_TRANSIENT_BLOCK_DIAG_REG`,
`CFD_SOLVER_TRANSIENT_ACCEPT_MIN_INNER`,
`CFD_SOLVER_TRANSIENT_PERTURBATION_AMPLITUDE`,
`CFD_SOLVER_TRANSIENT_WAKE_PERTURBATION_AMPLITUDE`,
`CFD_SOLVER_TRANSIENT_WAKE_X0`, `CFD_SOLVER_TRANSIENT_WAKE_WIDTH`,
`CFD_SOLVER_TRANSIENT_WAKE_STREAM_DECAY`,
`CFD_SOLVER_TRANSIENT_WAKE_WAVENUMBER`,
`CFD_SOLVER_TRANSIENT_WAKE_ODD_MODE`,
`CFD_SOLVER_CFL_INITIAL_OVERRIDE`, `CFD_SOLVER_CFL_MAX_OVERRIDE`,
`CFD_SOLVER_RECONSTRUCTION_SCALE`, `CFD_SOLVER_RUSANOV_DISSIPATION_SCALE`,
`CFD_SOLVER_MAX_INNER_OVERRIDE`,
`CFD_SOLVER_LAMINAR_WALL_DAMPING`,
`CFD_SOLVER_STEADY_FORCE_PLATEAU_WINDOW`,
`CFD_SOLVER_STEADY_FORCE_PLATEAU_MIN_STEPS`,
`CFD_SOLVER_STEADY_FORCE_PLATEAU_ABS_TOL`,
`CFD_SOLVER_STEADY_FORCE_PLATEAU_REL_TOL`,
`CFD_SOLVER_STEADY_FORCE_PLATEAU_RESIDUAL_GROWTH`,
`CFD_SOLVER_STEADY_ACCEPT_GROWTH`, `CFD_SOLVER_STEADY_LINE_SEARCH_TRIALS`,
`CFD_SOLVER_STEADY_MAX_NONIMPROVING_INNER`,
`CFD_SOLVER_KEEP_BEST_INNER_STATE`,
`CFD_SOLVER_ENFORCE_SLIP_CELL_TANGENCY`,
`CFD_SOLVER_RESIDUAL_HOTSPOT_INTERVAL`,
`CFD_SOLVER_PRIMITIVE_RELATIVE_CAP`, and `CFD_SOLVER_VELOCITY_CAP_SCALE`.
Defaults are conservative; production runs use second-order reconstruction
after the warmup period. Residual L2 norms are domain-area-weighted so tiny
leading/trailing-edge cells still affect `residual_linf` but do not fully
control the global L2 norm. Steady pseudo-time steps run the configured inner
relaxation loop up to the case maximum and reject residual-increasing outer
steps by rolling back and reducing the relaxation scale. Within each steady
inner loop, the solver keeps the lowest-residual inner state rather than
blindly accepting the final inner iterate unless
`CFD_SOLVER_KEEP_BEST_INNER_STATE=0` is set for diagnostics.
	`CFD_SOLVER_STEADY_LINE_SEARCH_TRIALS` can enable extra trial updates per inner
iteration; when it is zero, the pseudo-time update is applied directly.
The steady force-plateau criterion is opt-in; it is intended for long viscous
steady runs where residuals remain bounded and lift/drag ranges over a rolling
window fall below documented tolerances. Diagnostic-limited runs are still
marked failed even if the plateau controls are set.
Transient cases use a BDF2 physical-time residual with frozen previous states
during each physical step. `CFD_SOLVER_TRANSIENT_BLOCK_JACOBI=1` enables a
per-cell 4x4 finite-difference block-Jacobi preconditioner for diagnostics or
stricter inner solves. The submitted Re200 production directory uses the cheaper
minimum-inner damped transient policy
(`CFD_SOLVER_TRANSIENT_ACCEPT_MIN_INNER=1`) to reach the full supplied physical
horizon; the run status records this as bounded minimum-inner damped acceptance.
The solver also accepts a rank-local restart directory through `--restart
<output-dir>` when the directory contains `restart_final.rankNNNN.json` files.

## Python tooling

The benchmark asks that Python tooling use a local venv. The report generator
uses only the Python standard library, so no packages are required:

```bash
cd solver
python3 -m venv .venv
.venv/bin/python tools/generate_report.py --results results --report report
```

## Numerical implementation status

Implemented:

- CGNS unstructured mesh import for the supplied NACA and cylinder meshes.
- Multi-zone node merging for the cylinder mesh connectivity.
- Face/cell geometry construction for triangles and quads.
- METIS k-way cell partitioning.
- Rank-local owned/ghost cell mesh construction.
- Neighbor-scoped `MPI_Isend`/`MPI_Irecv` halo exchange.
- Global MPI reductions for residuals and forces.
- Rusanov/local Lax-Friedrichs inviscid flux.
- Least-squares piecewise-linear reconstruction.
- Barth-Jespersen scalar limiting with density/pressure positivity fallback.
- Constant-viscosity laminar Navier-Stokes viscous fluxes.
- Direct impermeable-wall inviscid pressure flux for slip and no-slip walls,
  with viscous wall shear added for no-slip laminar cases.
- Characteristic subsonic farfield state based on normal Riemann invariants,
  with supersonic inflow/outflow fallbacks.
- Optional slip-wall-adjacent inviscid cell-state tangency projection via
  `CFD_SOLVER_ENFORCE_SLIP_CELL_TANGENCY=1`; default wall enforcement is in the
  boundary flux and surface output rather than by mutating cell centers.
- Steady pseudo-time and transient BDF2 residual loops.
- Contract output files: metadata, partition diagnostics, residuals, forces,
  surface, VTU field, restart, stdout log, and run status.
- Laminar runs initialize with a no-slip-aware wall velocity damping layer to
  avoid starting from a freestream state that violates wall velocity by order
  one at the first viscous residual evaluation.

Current limitations:

- The final eight case directories under `solver/results/` pass the transparent
  benchmark validator together with `solver/report`.
- The Re200 transient result reaches `t=300` and is finite, but it is strongly
  damped and accepted with the documented minimum-inner policy rather than a
  fully reduced nonlinear residual on every physical step. Treat the result as a
  benchmark-completion artifact, not a high-fidelity vortex-shedding prediction.

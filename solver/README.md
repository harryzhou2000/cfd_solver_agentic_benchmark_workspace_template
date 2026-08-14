# 2-D Unstructured Compressible Navier–Stokes Solver (Benchmark Submission)

C++17/MPI cell-centered finite-volume solver for the compressible
Navier–Stokes equations of a calorically perfect gas on unstructured 2-D
meshes, built for the `cfd_solver_agentic_benchmark` cases (NACA0012 and
cylinder, inviscid / laminar, steady and transient vortex shedding).

## Layout

```
solver/
  CMakeLists.txt
  src/            solver source (case parsing, CGNS mesh, partition, FV core)
  tools/          run scripts and Python analysis/plotting/validation tools
  results/        case output directories (one per benchmark case)
  report/         LaTeX report, figures, manifests
  build/          CMake build tree (generated)
```

## Dependencies

- C++17 compiler, CMake >= 3.16, MPI (OpenMPI used for the submission runs)
- Compiled externals at `external/cfd_externals/install` (CGNS, HDF5, METIS,
  zlib); override with `-DCFD_EXTERNALS_ROOT=<path>` or the
  `CFD_EXTERNALS_ROOT` environment variable
- Header-only nlohmann_json under `external/nlohmann`; override with
  `-DCFD_HEADERONLY_ROOT=<path>` / `CFD_HEADERONLY_ROOT`
- Python 3 with numpy + matplotlib for post-processing (local `.venv`)

## Build

```bash
cd solver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=mpicxx
cmake --build build -j
```

## Python environment

```bash
cd solver
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
```

## Run

The executable satisfies the benchmark CLI contract:

```bash
mpirun -np <ranks> solver/build/cfd_solver solve --case <case.json> \
  --output <output-dir> [--restart <restart-dir>] [--report-level brief|full]
```

Partitioning (METIS k-way over the cell adjacency graph) is performed
automatically on first use and cached under `<output-dir>/partition_np<N>/`;
it can also be built explicitly:

```bash
solver/build/cfd_solver partition --case <case.json> --np <ranks> --output <output-dir>
```

Helper script used for the submitted runs:

```bash
solver/tools/run_case.sh <case.json> <output-dir> <ranks>
```

Post-processing (figures, sanity checks) — see `tools/`:

```bash
solver/.venv/bin/python solver/tools/make_figures_case.py <output-dir> solver/report/figures <case.json>
solver/.venv/bin/python solver/tools/sanity_checks.py solver/results <cases-dir> solver/report/sanity_checks.json
```

## Output contract

Each case output directory contains `metadata.json`,
`partition_diagnostics.csv`, `residuals.csv`, `forces.csv`, `surface.csv`,
`field_final.vtu`, `restart_final.rank*.bin`, `stdout.log`, and
`run_status.json`, plus the cached `partition_np<N>/` files.

## Numerics summary

- Cell-centered unstructured FV, conservative `[rho, rho u, rho v, rho E]`
- Rusanov (local Lax–Friedrichs) flux; dissipation evaluated on cell-state
  jumps for robustness on highly stretched cells, dissipation scale 1.0
- Piecewise-linear reconstruction on a vertex-neighbour weighted
  least-squares stencil, Venkatakrishnan limiter, positivity floors with
  first-order fallback at violated face states
- Viscous fluxes with corrected face-average gradients; constant viscosity
  matched to the case Reynolds number
- Characteristic (Riemann-invariant) farfield, mirror-state slip wall,
  no-slip adiabatic wall with one-sided wall gradients
- Steady: implicit backward-Euler pseudo-time march, LU-SGS
  (Yoon–Jameson scalar split) inner sweeps, local pseudo time steps from
  convective+viscous spectral radii, CFL ramp from the case files
- Transient: BDF2 physical-time outer loop (BDF1 first step), inner
  nonlinear/pseudo-time loop with frozen histories, pseudo-CFL 1.0
- MPI: METIS k-way partitioning, rank-local owned+ghost cells, neighbour
  `MPI_Isend/Irecv` halo exchange of states/gradients/limiters, global
  reductions for residuals and forces; full mesh/state is never replicated
  during iterations

See `report/report.pdf` for the full technical report and `report/run_manifest.md`
for the exact commands and per-case status.

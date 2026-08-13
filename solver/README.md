# cfd_solver — 2-D Unstructured Compressible Navier–Stokes Solver

An original C++17/MPI finite-volume solver for the two-dimensional
compressible Navier–Stokes equations (calorically perfect gas), built for the
CFD solver agentic benchmark. It reads the supplied CGNS meshes, partitions
the cell adjacency graph with METIS, and runs every required case to a
converged or statistically periodic state.

## Dependencies

- CMake >= 3.16, a C++17 compiler, OpenMPI (or another MPI-3 implementation)
- The benchmark's external libraries under
  `external/cfd_externals/install/` (CGNS, HDF5, METIS; headers for
  nlohmann_json are also used). Point CMake at them with
  `-DCFD_EXTERNALS_ROOT=<install prefix>`.
- Python 3 (only for figure and report tooling in `tools/`). The tooling runs
  inside the solver's local virtual environment:

  ```bash
  cd solver
  python3 -m venv .venv
  .venv/bin/pip install numpy matplotlib
  ```

  Use `.venv/bin/python` for every script in `tools/` (the `.venv/` directory
  is git-ignored). The dependency list is `numpy` and `matplotlib`.

## Build

```bash
cmake -S solver -B solver/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFD_EXTERNALS_ROOT=<repo>/external/cfd_externals/install
cmake --build solver/build -j
```

The executable is `solver/build/cfd_solver`.

## Run

All eight benchmark cases use the same CLI:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/<case>.json \
  --output solver/results/<case_id>
```

Optional flags used for the production runs (documented in
`report/run_manifest.md` and the per-case `metadata.json`):

- `--first-order` — first-order reconstruction everywhere (stability for
  supersonic/viscous cases).
- `--wall-first` — first-order for wall-adjacent cells, second-order
  elsewhere.
- `--cfl-cap <v>` — cap the pseudo-time CFL ramp.
- `--max-inner <n>` — cap LU-SGS inner sweeps per step.
- `--max-steps <n>` — override the case-file `max_steps` (used by the
  rank-count validation runs).
- `--field-interval <n>` — write intermediate VTU fields every n steps.
- `--restart <file>` — resume from a restart file (the solver writes
  `restart_checkpoint.bin` every 500 steady steps / 100 transient steps and
  `restart_final.bin` at the end).

Environment controls used in production:

- `CFD_SWEEPS=1` — one LU-SGS sweep per dual-time inner iteration (the
  multi-sweep accumulation is unstable from the freestream start).
- `CFD_INNER_CHECK=1` — evaluate the inner residual every inner iteration.
- `CFD_WALL_PBLEND` — wall-pressure interior extrapolation weight for no-slip
  walls (default 1.0: zero-normal-pressure-gradient extrapolation).

Run `solver/run/run_production.sh` to reproduce all eight case runs
sequentially, or `solver/run/run_sequential.sh` for the previous settings.

## Outputs

Each case directory contains `metadata.json`, `partition_diagnostics.csv`,
`residuals.csv`, `forces.csv`, `surface.csv`, `field_final.vtu`,
`restart_final.bin`, `stdout.log`, and `run_status.json`, per the benchmark
output contract.

## Tools

- `tools/make_figures.py` — report figures + `figure_manifest.csv`.
- `tools/make_report_aux.py` — `sanity_checks.json` + `run_manifest.md`.
- `tools/fix_metadata.py` — repair metadata JSON written by an intermediate
  build (field-order bug, fixed in the final source).
- `tools/clamp_field.py` — clamp field VTU pressure to the solver's
  positivity floor for files written by older builds.

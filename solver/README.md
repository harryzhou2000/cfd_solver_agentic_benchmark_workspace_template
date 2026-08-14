# cfdsolve — 2-D Unstructured Compressible Navier–Stokes Solver (Benchmark Solution)

C++17/MPI cell-centered finite-volume solver for the compressible Navier–Stokes
equations of a calorically perfect gas, built for the CFD solver agentic
benchmark (`cfd_solver_agentic_benchmark/`). Features METIS domain
decomposition with neighbor-scoped halo exchange, second-order reconstruction
with limiters, LU-SGS implicit pseudo-time integration, and BDF2 dual-time
stepping for transient cases.

## Dependencies

- Linux, CMake ≥ 3.16, C++17 compiler, MPI (OpenMPI used for testing)
- Compiled externals under `external/cfd_externals/install/` (CGNS, HDF5,
  METIS, zlib) — DNDSR external-dependency convention
- Header-only packages under `external/` (nlohmann_json used; Eigen available)
- Python 3 with a local virtual environment for plotting/validation:
  `numpy`, `matplotlib`

## Build

```bash
export PATH=<openmpi>/bin:$PATH
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=$(which mpicxx) \
      -DCFD_EXTERNALS_ROOT=$PWD/../external/cfd_externals/install \
      -DCFD_HEADERONLY_ROOT=$PWD/../external
cmake --build build -j
```

Both `CFD_EXTERNALS_ROOT` and `CFD_HEADERONLY_ROOT` may also be given as
environment variables; the defaults are `../external/cfd_externals/install`
and `../external` relative to the solver directory.

## Python environment

```bash
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
```

All Python tools are run with the venv interpreter, e.g.
`.venv/bin/python tools/plot_results.py ...`.

## Run

```bash
export LD_LIBRARY_PATH=$PWD/../external/cfd_externals/install/lib:$LD_LIBRARY_PATH
mpirun -np <ranks> build/cfdsolve solve --case <case.json> --output <out-dir> \
    [--restart <restart.bin>] [--report-level brief|full] \
    [--flux rusanov|hllc] [--cfl-initial X] [--cfl-max X] [--ramp-steps N]
```

The same executable and command format runs every supplied case. All eight
benchmark cases are produced by:

```bash
tools/run_all.sh results 8 steady     # 7 steady cases
tools/run_all.sh results 8 transient  # cylinder Re 200 (dt=0.01, t=300, BDF2)
```

### Note on CFL schedules (documented deviation)

The case JSONs recommend pseudo-time CFL ramps up to 50–100. With the present
scalar-Jacobian LU-SGS outer iteration those caps are not stable on the
supplied meshes, so production steady runs use stricter fixed CFL values
(0.2–1.0, see `tools/run_all.sh` and the report). All other production
parameters (max steps, residual targets, Re 200 transient controls) are used
exactly as supplied. Transient runs restart from `restart_latest.bin`
(written every 1000 physical steps) via `--restart`.

## Outputs

Each case output directory follows `OUTPUT_CONTRACT.md`: `metadata.json`,
`partition_diagnostics.csv`, `residuals.csv`, `forces.csv`, `surface.csv`,
`field_final.vtu` (binary appended, contains density, velocity, pressure,
Mach, temperature, rank), `restart_final.bin`, `stdout.log`, `run_status.json`.

## Post-processing and report

```bash
.venv/bin/python tools/plot_results.py --results results \
    --figures report/figures --manifest report/figure_manifest.csv
.venv/bin/python tools/sanity_checks.py --results results \
    --out report/sanity_checks.json --manifest report/figure_manifest.csv
python3 ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py results/* --report report
pdflatex -output-directory report report/report.tex
```

## Source layout

- `src/cgns_mesh.*` — CGNS multi-zone reader, conformal zone node merging,
  face/edge construction, boundary family tags
- `src/distribute.*` — METIS k-way partitioning, rank-local mesh with ghost
  layer and halo maps, partition diagnostics
- `src/physics.hpp` — gas model, Rusanov/HLLC fluxes, boundary states
- `src/solver.*` — gradients/reconstruction/limiters, residual assembly,
  viscous fluxes, LU-SGS, steady pseudo-time loop, BDF2 dual-time loop,
  forces and surface values
- `src/output.*` — VTU/restart/surface/partition writers
- `src/main.cpp` — CLI and case orchestration
- `tools/` — run scripts, plotting, sanity checks, mesh inspector

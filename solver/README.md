# 2-D Unstructured Compressible Navier--Stokes Solver

An original C++17/MPI finite-volume solver for the compressible
Navier--Stokes equations of a calorically perfect gas, developed for the
CFD solver agentic benchmark. The solver reads CGNS meshes, partitions the
cell graph with METIS, and solves steady and transient cases with implicit
pseudo-time / BDF2 dual-time integration.

## Dependencies

- C++17 compiler (g++ 13+)
- CMake >= 3.16
- OpenMPI (mpicxx, mpirun)
- External libraries under the DNDSR convention, configurable via
  `-DCFD_EXTERNALS_ROOT=<install-prefix>`:
  - CGNS (`include/cgnslib.h`, `lib/libcgns`)
  - METIS (`include/metis.h`, `lib/libmetis`)
  - HDF5 (used by CGNS)
  - Eigen (header-only, `external/eigen`)
  - nlohmann_json (header-only, `external/nlohmann`)
  - argparse (header-only, `external/argparse`)
- Python 3 + numpy + matplotlib in `solver/.venv` for plotting and report
  automation

## Build

```bash
cd <workspace>/solver
cmake -S . -B build -DCFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install
make -C build -j$(nproc)
```

This produces `build/cfd_solver` and `build/mesh_probe`.

## Run

```bash
mpirun -np <ranks> build/cfd_solver solve \
    --case <workspace>/cfd_solver_agentic_benchmark/inputs/cases/<case>.json \
    --output <output-dir>
```

The same command works for every supplied case. Optional
`--restart <file>` resumes from a restart file and `--report-level brief|full`
selects output verbosity.

Production runs are orchestrated by `tools/run_all.sh` (all 8 cases at np=8)
with the documented plateau convergence configuration:

```bash
export CFD_LUSGS_RELAX=0.4
export CFD_LUSGS_DIAG_FACTOR=2.0
export CFD_PHYS_CFL_CAP=50
export CFD_PLATEAU_WINDOW=2500
export CFD_PLATEAU_MIN_ORDERS=0.5
bash tools/run_all.sh
```

`naca0012_m015_inviscid` is run with the block-Jacobi inner solver (the
`CFD_USE_LUSGS` env var unset); all other cases use the scalar LU-SGS path.

## Outputs

Each output directory contains:

- `metadata.json` -- solver/case metadata and convergence status
- `run_status.json` -- run command, wall time, final step, status
- `partition_diagnostics.csv` -- per-rank owned/ghost cells, neighbors
- `residuals.csv` -- global residual history
- `forces.csv` -- force coefficient history
- `surface.csv` -- wall surface data (boundary values)
- `field_final.vtu` -- final flow field (cell-centered, ASCII VTK)
- `restart_final.*` -- rank-local restart state
- `stdout.log` -- captured solver output

## Plots and report

```bash
solver/.venv/bin/python3 solver/tools/plot_results.py solver/results solver/report
solver/.venv/bin/python3 solver/tools/generate_report.py solver/results solver/report
cd solver/report && pdflatex report.tex
```

## Validation

```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
    solver/results/* --report solver/report
```

## Repository layout

```text
solver/
  CMakeLists.txt
  src/            solver source (case I/O, mesh, partition, numerics,
                  drivers, output, main)
  tools/          production launcher, plotting, report generation
  results/        per-case output directories
  report/         LaTeX report, figures, manifests, sanity checks
```

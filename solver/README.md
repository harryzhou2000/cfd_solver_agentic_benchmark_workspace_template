# CFD Solver for the Compressible Navier-Stokes Equations

A 2-D cell-centered finite-volume solver for the compressible Navier-Stokes
equations on unstructured mixed-element grids, written in C++17 with MPI.

## Dependencies

- C++17 compiler (GCC 9+)
- OpenMPI (or MPICH)
- CMake 3.16+
- CGNS, HDF5, METIS (provided under `../external/cfd_externals/install/`)
- Eigen, nlohmann-json (header-only, under `../external/`)
- Python 3 with numpy and matplotlib (for plotting)

## Build

```bash
cd solver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCFD_EXTERNALS_ROOT=$(pwd)/../external/cfd_externals/install
cmake --build build -j
```

## Run

```bash
export LD_LIBRARY_PATH=$(pwd)/../external/cfd_externals/install/lib:${LD_LIBRARY_PATH}
mpirun -np 8 build/cfd_solver solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid
```

All eight supplied cases can be run with the same executable. Case-specific
settings (CFL schedule, inner iterations, physical time step) come from the
case JSON files.

Optional debug overrides (for diagnostics, not needed for production):
`--max-steps N`, `--cfl-cap C`, `--max-inner N`, `--first-order`.

## Output

Each run writes into the output directory:

- `metadata.json` — solver and run metadata
- `partition_diagnostics.csv` — per-rank owned/ghost/boundary counts
- `residuals.csv` — step-by-step residual history
- `forces.csv` — force coefficient history
- `surface.csv` — wall boundary surface rows
- `field_final.vtu` — final flow field (VTK XML)
- `restart_final.bin` — binary restart state
- `run_status.json` — run summary

## Validation

```bash
python3 ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py results/<case>
```

## Report

The LaTeX report source is in `report/report.tex`; figures are generated with:

```bash
python3 tools/make_figures.py <case_id> results/<case> --out_dir report/figures
```

# cfd2d: 2-D Unstructured Compressible Finite-Volume Solver

This submission implements a cell-centred unstructured finite-volume solver
for the compressible Navier-Stokes equations in C++17 with MPI. CGNS/HDF5
mesh input, METIS k-way partitioning, neighbor-scoped halo exchange, Rusanov
inviscid fluxes, Newtonian/Fourier viscous fluxes, least-squares reconstruction,
Venkatakrishnan or Barth-Jespersen limiting, positivity damping, nonlinear
LU-SGS pseudo-time iteration, and dual-time BDF2 transient stepping are kept
in separate source modules under `src/`.

## Dependencies

- C++17 compiler and CMake 3.16+
- MPI (OpenMPI or MPICH)
- CGNS, HDF5, METIS and zlib under `external/cfd_externals/install`
- nlohmann-json under `external/nlohmann`
- Python 3.10+, NumPy, SciPy, Matplotlib, h5py (for report tooling)

Create the local Python environment:

```bash
cd solver
python3 -m venv .venv
.venv/bin/pip install numpy scipy matplotlib h5py
```

## Build

```bash
cmake -S solver -B solver/build -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install"
cmake --build solver/build -j
```

## Run

```bash
mpirun -np 8 solver/build/cfd2d solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output solver/results/naca0012_m015_inviscid
```

The same command works for every supplied JSON case. Optional diagnostic
overrides are `--first-order`, `--limiter barth-jespersen|venkatakrishnan|none`,
`--cfl-cap C`, and `--max-inner N`; these are recorded in the command line and
should not be used silently for final production results.

## Outputs

Each completed case directory contains `metadata.json`,
`partition_diagnostics.csv`, `residuals.csv`, `forces.csv`, `surface.csv`,
`field_final.vtu`, `restart_final.bin`, `stdout.log`, and `run_status.json`.
`field_final.vtu` is an ASCII VTK unstructured-grid file readable by ParaView,
VisIt, or standard XML readers.

## Reproducible launcher

```bash
solver/tools/run_cases.sh 8 solver/results
```

The launcher runs all eight cases with the supplied JSON controls and records
each command's stdout in the case directory. Use separate output directories
for diagnostic short runs and final results.

## Report and validation

The examiner's structural validator is run as:

```bash
solver/.venv/bin/python cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  solver/results/<case-id> --report solver/report
```

Figures and the LaTeX report are generated from submitted CSV/VTU files by the
scripts in `tools/`; every report figure is listed in `report/figure_manifest.csv`.

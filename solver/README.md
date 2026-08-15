# CFD Solver

Two-dimensional compressible CFD solver (finite-volume, CGNS input,
MPI + METIS/ParMETIS parallelism). Phase 1: project skeleton with a
compilable CLI entry point and stub modules.

## Directory layout

```
solver/
  CMakeLists.txt
  src/                 C++ sources (main + module stubs)
  tools/plot_results.py
  results/             Solver output landing spot
```

## Build

Requires: g++-13 with OpenMPI 5.0.9, CMake >= 3.16. Compiled externals
(CGNS, METIS, ParMETIS, HDF5) live in `external/cfd_externals/install`
relative to the repo root; header-only libs (Eigen, nlohmann/json, fmt)
live in `external/`.

```bash
cd solver
mkdir build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=../external/cfd_externals/install
make -j$(nproc)
```

Override the defaults with `-DCFD_EXTERNALS_ROOT=<path>` and
`-DEXTERNAL_DIR=<path>` if your externals are elsewhere. CMake prints a
dependency summary on configure; run `./cfd_solver --help` after building.

## Run

```bash
# from solver/build
mpirun -np 4 ./cfd_solver --case ../../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json --output ./results/test_run
```

The solve loop is a Phase 1 stub; with `--case` the binary currently
validates and prints the parsed case parameters only.

## Python Environment

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

Post-processing helpers live in `tools/`:

```bash
python tools/plot_results.py --help
```

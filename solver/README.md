# 2-D Unstructured Finite-Volume CFD Solver

C++17/MPI compressible Navier-Stokes solver for the CFD Solver Agentic Benchmark.

## Dependencies

- C++17 compiler (g++-13 or newer)
- OpenMPI 5.0+
- CMake 3.16+
- CGNS 4.5, HDF5, METIS, zlib (pre-built at `external/cfd_externals/install/`)
- Eigen 3, fmt, nlohmann_json, argparse (header-only, at `external/`)
- Python 3 (for plotting and report generation)

## Build

```bash
cd solver
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCFD_EXTERNALS_ROOT=$(pwd)/../../external/cfd_externals/install
make -j$(nproc)
```

## Run

```bash
# Single case
mpirun -np 4 ./build/cfd_solver solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid

# With max steps override
mpirun -np 1 ./build/cfd_solver solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json \
  --output results/cylinder_m010_laminar_re20 \
  --max-steps 500

# Transient case (BDF2)
mpirun -np 2 ./build/cfd_solver solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  --output results/cylinder_m010_laminar_re200
```

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

## Run All Cases

```bash
cd solver
.venv/bin/python tools/run_cases.py
```

## Plot Results

```bash
cd solver
.venv/bin/python tools/plot_results.py \
  --case-dir results/naca0012_m015_inviscid/ \
  --output-dir report/figures/ \
  --case-id naca0012_m015_inviscid --all
```

## Validate Outputs

```bash
cd solver
.venv/bin/python ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  results/naca0012_m015_inviscid results/cylinder_m010_laminar_re20 --report report/
```

## Run Tests

```bash
cd solver/build && ctest --output-on-failure
```

## Key Numerics

- Cell-centered unstructured finite volume, second-order LSQ reconstruction
- Barth-Jespersen limiter on primitive variables
- Rusanov (Local Lax-Friedrichs) inviscid flux with configurable dissipation scale
- Block LU-SGS implicit solver (Yoon-Jameson split)
- BDF2 dual-time stepping for transient cases
- METIS k-way cell-graph partitioning, neighbor-scoped Isend/Irecv halo exchange

## Output

Each case directory follows `OUTPUT_CONTRACT.md`:
- `residuals.csv`, `forces.csv`, `surface.csv` — per-step CSV data
- `field_final.pvtu` + per-rank `.vtu` pieces — VTK unstructured grid
- `metadata.json`, `run_status.json`, `partition_diagnostics.{csv,json}`
- `restart_final.json` — restart file
- `stdout.log` — captured console output

## Report

```bash
cd solver/report && pdflatex report.tex && pdflatex report.tex
```

# 2D Unstructured Compressible Navier-Stokes CFD Solver

Cell-centered finite-volume solver for the compressible Navier-Stokes equations
on unstructured 2D meshes. Uses MPI domain decomposition with METIS partitioning.

## Dependencies

- **C++17** compiler (g++ 13+)
- **MPI** (Open MPI 5.0+)
- **CGNS** 4.5+ (mesh I/O)
- **HDF5** 1.14+ (CGNS backend)
- **METIS** / **ParMETIS** (graph partitioning)
- **Python 3** (plotting and report automation)

Header-only libraries (bundled under `../external/`):
- Eigen 5.0, fmt 11.1, nlohmann/json 3.12, argparse 3.2

## Build

```bash
cd solver
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Override external library paths:
```bash
cmake .. -DCFD_EXTERNALS_ROOT=/path/to/cfd_externals/install
```

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

## Run

```bash
# Single-rank steady case
./build/cfd_solver solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
    --output results/naca0012_m015_inviscid

# Multi-rank transient case
mpirun -np 4 ./build/cfd_solver solve \
    --case ../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
    --output results/cylinder_m010_laminar_re200
```

## Output

Each case run produces:
- `residuals.csv` — residual history
- `forces.csv` — force coefficient history
- `surface.csv` — wall surface distributions
- `field_final.vtu` — final flow field (VTK XML Unstructured)
- `metadata.json` — solver and run metadata
- `run_status.json` — run summary
- `partition_diagnostics.csv` — per-rank partition info

## License

This is an original implementation created for the CFD Solver Agentic Benchmark.

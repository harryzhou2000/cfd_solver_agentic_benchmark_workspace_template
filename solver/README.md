# 2-D Unstructured CFD Solver — Benchmark Submission

## Dependencies
- C++17 compiler (g++ 13+)
- CMake 3.28+
- OpenMPI 5.0+
- CGNS 4.5, HDF5, METIS 5.1, zlib (compiled, at `external/cfd_externals/install/`)
- Eigen, fmt, nlohmann_json (header-only, at `external/`)
- Python 3 with numpy, matplotlib (for plots and validation)

## Build
```bash
cd solver
cmake -S . -B build -DCFD_EXTERNALS_ROOT=../external/cfd_externals/install
cmake --build build -j$(nproc)
```

## Run a Single Case
```bash
export LD_LIBRARY_PATH=$(realpath ../external/cfd_externals/install/lib):$LD_LIBRARY_PATH
mpirun -np 4 ./build/cfd_solver solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid
```

## Run All Cases (Production)
```bash
./run_cases.sh [NP_RANKS] [CASE_DIR]
# Example: ./run_cases.sh 4
```

## Output Files Per Case
- metadata.json — solver metadata and configuration
- run_status.json — convergence status and wall time
- residuals.csv — per-step residual history
- forces.csv — per-step force coefficients
- surface.csv — wall surface distributions
- field_final.vtu — final flow field (VTK UnstructuredGrid)
- partition_diagnostics.csv — per-rank partition data
- restart_final.bin — binary restart file
- stdout.log — solver output log

## Validation
```bash
cd ../cfd_solver_agentic_benchmark
python3 examiner/validate_outputs.py results/<case_id>
```

## Generate Plots
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
python tools/plot_all.py
```

## Report
LaTeX source: `report/report.tex` — compile with pdflatex.
Figures: `report/figures/`
Manifests: `report/run_manifest.csv`, `report/figure_manifest.csv`, `report/sanity_checks.json`

## Solver Features
- C++17/MPI, cell-centered finite volume
- METIS graph partitioning (rank-local owned+ghost cells)
- Second-order least-squares reconstruction with Barth-Jespersen limiter
- Rusanov (LLF) and Roe approximate Riemann solvers with Harten-Yee entropy fix
- Newtonian viscous stresses with Fourier heat conduction
- Characteristic farfield, slip wall, no-slip adiabatic wall BCs
- Block-diagonal LU-SGS implicit solver with CFL ramp
- BDF2 transient solver with frozen-history two-level loop
- Complete output contract per benchmark specification

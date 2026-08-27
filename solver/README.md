# CFD2D - 2D Unstructured Compressible Navier-Stokes Solver

A cell-centered finite-volume solver for the 2D compressible Navier-Stokes equations on unstructured meshes with MPI parallelization.

## Features

- Cell-centered finite volume method on unstructured triangular/quadrilateral meshes
- Roe approximate Riemann solver with Harten-Yee entropy fix
- Second-order reconstruction with least-squares gradients
- Barth-Jespersen limiter with positivity fallback
- Viscous fluxes with corrected face gradients
- LU-SGS implicit solver with CFL ramping
- BDF2 physical-time integration for transient cases
- METIS graph partitioning with neighbor halo exchange (MPI)
- CGNS mesh input, VTK field output, CSV residual/force/surface output

## Dependencies

- C++17 compiler with MPI support
- CMake >= 3.16
- CGNS library with HDF5
- METIS / ParMETIS
- Eigen (header-only)
- nlohmann_json (header-only)
- Python 3 with numpy, matplotlib (for plotting)

## Build

```bash
cd solver
mkdir build && cd build
cmake .. \
    -DCFD_EXTERNALS_ROOT=/workspace/external/cfd_externals/install \
    -DEIGEN_ROOT=/workspace/external/eigen \
    -DNLOHMANN_ROOT=/workspace/external/nlohmann \
    -DFMT_ROOT=/workspace/external/fmt
make -j8
```

## Run

```bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH

# Single case
mpirun --allow-run-as-root -np 4 ./build/cfd2d solve \
    --case /workspace/cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
    --output results/naca0012_m015_inviscid

# All cases
bash tools/run_all_cases.sh
```

## Post-processing

```bash
source .venv/bin/activate
python tools/plot_results.py
```

## Python Environment Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib
```

## Output Structure

Each case produces:
- `metadata.json` - solver metadata and configuration
- `residuals.csv` - convergence history
- `forces.csv` - force coefficients
- `surface.csv` - wall surface data
- `field_final.vtk` - flow field
- `partition_diagnostics.csv` - MPI partition info
- `run_status.json` - run summary
- `stdout.log` - console output

## Report

LaTeX report source in `report/report.tex`. Compile with:
```bash
cd report && pdflatex report.tex && pdflatex report.tex
```

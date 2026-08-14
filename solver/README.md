# CFD Solver: 2-D Unstructured Compressible Navier-Stokes

## Overview
A C++17/MPI finite-volume solver for the 2-D compressible Navier-Stokes equations on unstructured meshes. Features Roe/Rusanov approximate Riemann fluxes, Barth-Jespersen limiter with second-order least-squares reconstruction, LU-SGS implicit time integration, and BDF2 dual-time stepping for transient cases.

## Dependencies
- C++17 compiler (GCC 13+)
- MPI (OpenMPI 5.0+)
- CMake 3.16+
- CGNS library (provided in external/cfd_externals/install)
- METIS library (provided in external/cfd_externals/install)
- HDF5 library (provided in external/cfd_externals/install)
- Eigen, fmt, nlohmann_json (header-only, provided in external/)
- Python 3 with numpy and matplotlib (for plotting)
- pdflatex (for report compilation)

## Build
```bash
export CFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install
export EXTERNAL_HEADER_ROOT=<workspace>/external
export PATH=<mpi-install>/bin:$PATH
cd solver
mkdir build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=$CFD_EXTERNALS_ROOT -DEXTERNAL_HEADER_ROOT=$EXTERNAL_HEADER_ROOT
make -j16
```

## Run
```bash
export LD_LIBRARY_PATH=<mpi-install>/lib:<workspace>/external/cfd_externals/install/lib:$LD_LIBRARY_PATH

# Steady case
mpirun -np 4 solver/build/cfd_solver solve \
    --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
    --output solver/results/naca0012_m015_inviscid \
    --rusanov --cfl-cap 3 --sweeps 1 --first-order-steps 1000 \
    --max-limiter 0.15 --diag-safety 1.5 --max-steps 5000

# Transient case
mpirun -np 4 solver/build/cfd_solver solve \
    --case cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
    --output solver/results/cylinder_m010_laminar_re200 \
    --rusanov --cfl-cap 2 --max-limiter 0.0 --diag-safety 1.0 --max-inner 1
```

## CLI Options
--case <json>     : case file path
--output <dir>    : output directory
--rusanov         : use Rusanov flux (default: Roe)
--first-order     : force first-order reconstruction
--cfl-cap <val>   : cap CFL at this value
--sweeps <n>      : LU-SGS sweeps per step
--max-steps <n>   : override max steps
--max-inner <n>   : override max inner iterations
--max-limiter <v> : cap limiter value
--diag-safety <v> : LU-SGS diagonal safety factor
--global-dt       : use global (minimum) time step

## Output Files
Each case output directory contains:
- metadata.json, run_status.json, partition_diagnostics.csv
- residuals.csv, forces.csv, surface.csv
- field_final.vtu, restart_final.dat, stdout.log

## Plotting and Report
```bash
python3 solver/tools/plot_results.py solver/results solver/report/figures
python3 solver/tools/generate_report_data.py solver/results solver/report
cd solver/report && pdflatex report.tex
```

## Validation
```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
    solver/results/<case1> solver/results/<case2> ... --report solver/report
```

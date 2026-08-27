# 2-D Unstructured Compressible CFD Solver

A C++17/MPI finite-volume solver for the 2-D compressible Navier-Stokes equations on
unstructured meshes. Implements second-order spatial reconstruction, Rusanov inviscid flux,
LU-SGS implicit time stepping, and BDF2 physical-time integration for unsteady flows.

## Dependencies

- MPI (OpenMPI or MPICH)
- CGNS/HDF5 (from `external/cfd_externals/install/`)
- METIS (from `external/cfd_externals/install/`)
- nlohmann/json (header-only, from `external/`)
- CMake >= 3.16, C++17 compiler

## Build

```bash
cd solver
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release       -DCFD_EXTERNALS_ROOT=../../external/cfd_externals/install       ..
make -j$(nproc)
```

The compiled executable will be at `solver/build/cfd_solver`.

## Run

```bash
mpirun -np <N> solver/build/cfd_solver solve   --case <path/to/case.json>   --output <output-directory>
```

**Optional flags:**
- `--restart <restart-file>`: resume from a restart manifest
- `--report-level brief|full`: verbosity level

## Generate Figures and Report

```bash
# Generate figures from result field files and CSV histories
python3 solver/report/gen_figures.py

# Update run manifest and sanity checks
python3 solver/report/gen_report.py

# Compile the LaTeX report (requires pdflatex)
cd solver/report && pdflatex -interaction=nonstopmode report.tex
```

## Output Files per Case

Each `--output` directory contains:

| File | Description |
|------|-------------|
| `metadata.json` | Solver settings, partition info, inner-iteration stats |
| `run_status.json` | Final step, wall time, convergence status |
| `residuals.csv` | Per-step residual norms (L2, L-inf) |
| `forces.csv` | Per-step Cl, Cd, Cmz, pressure/viscous split |
| `surface.csv` | Wall boundary: x, y, Cp, Cf, density, velocity, Mach |
| `field_final.pvtu` | Final flow field (VTK unstructured, all ranks) |
| `restart_final.*` | Restart manifest + per-rank binary state |
| `partition_diagnostics.csv` | METIS partition info per rank |

## Required Cases

Run all 8 cases from `cfd_solver_agentic_benchmark/inputs/cases/`:

```bash
for CASE in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
            naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 \
            naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20 \
            cylinder_m010_laminar_re200; do
  mpirun -np 1 solver/build/cfd_solver solve \
    --case cfd_solver_agentic_benchmark/inputs/cases/${CASE}.json \
    --output solver/results/${CASE}
done
# Note: cylinder_m010_laminar_re200 was run with -np 4
```

## Validate

```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py \
  solver/results/naca0012_m015_inviscid \
  solver/results/naca0012_m080_inviscid \
  solver/results/naca0012_m200_inviscid \
  solver/results/naca0012_m015_laminar_re5000 \
  solver/results/naca0012_m080_laminar_re5000 \
  solver/results/naca0012_m200_laminar_re5000 \
  solver/results/cylinder_m010_laminar_re200 \
  solver/results/cylinder_m010_laminar_re20 \
  --report solver/report
```

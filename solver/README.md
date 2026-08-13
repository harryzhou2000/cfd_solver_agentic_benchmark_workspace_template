# CFD2D: 2D Unstructured Compressive Navier-Stokes Solver

## Overview
A cell-centered finite-volume solver for the 2D compressible Navier-Stokes equations on unstructured meshes, implemented in C++17 with MPI parallelism.

## Features
- **Mesh**: CGNS reader supporting mixed tri/quad elements and multi-zone meshes
- **Partitioning**: METIS k-way graph partitioning with ghost cell construction
- **MPI**: Neighbor-scoped halo exchange (Isend/Irecv), no full-state replication
- **Inviscid flux**: Roe approximate Riemann solver with Harten-Yee entropy fix (Rusanov fallback)
- **Reconstruction**: Second-order Green-Gauss gradients with Barth-Jespersen limiter
- **Viscous flux**: Newtonian stress tensor, Fourier heat flux, constant viscosity matching Re
- **Implicit**: Point-implicit (block-Jacobi) with local time stepping
- **Transient**: BDF2 dual-time stepping with inner iterations for unsteady cases
- **Boundary conditions**: Farfield (Riemann), slip wall (inviscid), no-slip adiabatic wall (viscous)
- **Output**: CSV (residuals, forces, surface, partition), VTU field, metadata, restart

## Build
```bash
cd solver
mkdir build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=<path-to-cfd_externals/install> \
         -DEXTERNAL_HEADER_ROOT=<path-to-external>
make -j8
```

## Run
```bash
mpirun -np <ranks> solver/build/cfd2d solve \
    --case <case-json> \
    --output <output-dir> \
    [--restart <restart-file>] \
    [--report-level brief|full]
```

## Python Environment
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib h5py
```

## Dependencies
- C++17 compiler (g++ 13+)
- MPI (OpenMPI 5.0+)
- CMake 3.16+
- CGNS, HDF5, METIS (from cfd_externals)
- Eigen, fmt, nlohmann_json (header-only)
- Python 3 with numpy, matplotlib, h5py (for plotting/report)

## Case Files
The solver reads JSON case files from `cfd_solver_agentic_benchmark/inputs/cases/`.
See `INPUT_FORMAT.md` for the schema.

## Output Contract
Each case produces: metadata.json, run_status.json, partition_diagnostics.csv,
residuals.csv, forces.csv, surface.csv, field_final.vtu, restart_final.bin.
See `OUTPUT_CONTRACT.md` for details.

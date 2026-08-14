# cfd2d: 2-D Unstructured Compressible Navier-Stokes Solver

A C++17/MPI finite-volume solver for the 2-D compressible Navier-Stokes equations
on unstructured grids, built from scratch for the CFD agentic benchmark.

## Dependencies

- C++17 compiler (GCC 13+ tested)
- CMake 3.16+
- MPI (OpenMPI 5.0 tested)
- CGNS library (for mesh I/O)
- METIS (for graph partitioning)
- HDF5 (CGNS backend)
- zlib
- Eigen (header-only, for linear algebra)
- nlohmann/json (header-only, for case file parsing)
- Python 3 with numpy, matplotlib, h5py (for plotting/reporting)

All libraries are provided under `external/cfd_externals/install/` and
`external/` following the DNDSR convention.

## Build

```bash
cd solver
mkdir build && cd build
cmake .. \
  -DCFD_EXTERNALS_ROOT=<workspace>/external/cfd_externals/install \
  -DEIGEN_ROOT=<workspace>/external/eigen \
  -DNLOHMANN_ROOT=<workspace>/external/nlohmann \
  -DFMT_ROOT=<workspace>/external/fmt
make -j4
```

## Python Environment

```bash
cd solver
python3 -m venv .venv
source .venv/bin/activate
pip install numpy matplotlib h5py
```

## Run a Single Case

```bash
export LD_LIBRARY_PATH=<workspace>/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
mpirun -np 4 ./build/cfd2d solve \
  --case <workspace>/cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid
```

## Run All Cases

```bash
cd solver
.venv/bin/python3 tools/run_all.py
```

## Generate Report Figures and Artifacts

```bash
cd solver
.venv/bin/python3 tools/generate_report.py
```

## Solver Architecture

- `src/types.h`: Core data structures (ConsState, PrimState, Face, BCType)
- `src/case_io.cpp`: JSON case file parser
- `src/cgns_reader.cpp`: CGNS multi-zone mesh reader with vertex merging
- `src/mesh.cpp`: Cell-face connectivity, geometry, interior cell exclusion
- `src/partition.cpp`: METIS graph partitioning, rank-local mesh, halo exchange maps
- `src/fluxes.cpp`: Gas physics, Rusanov inviscid flux, Newtonian viscous flux
- `src/reconstruction.cpp`: Green-Gauss gradients, Barth-Jespersen limiter
- `src/implicit.cpp`: LU-SGS implicit solver with viscous spectral radius
- `src/solver.cpp`: Residual assembly, boundary conditions, force computation, halo exchange
- `src/output.cpp`: CSV/VTU/metadata output
- `src/main.cpp`: Steady pseudo-time and BDF2 transient loops

## Numerical Method

- **Equations**: 2-D compressible Navier-Stokes, calorically perfect gas
- **Spatial**: Cell-centered FV, second-order Green-Gauss reconstruction with Barth-Jespersen limiter
- **Inviscid flux**: Rusanov/local Lax-Friedrichs
- **Viscous flux**: Newtonian stress tensor, Fourier heat flux, constant viscosity
- **Time integration**: Pseudo-time (steady) with LU-SGS; BDF2 (transient) with inner LU-SGS iterations
- **Implicit solver**: LU-SGS with simplified spectral-radius Jacobian, viscous spectral radius, under-relaxation
- **Partitioning**: METIS k-way graph partitioning
- **Halo exchange**: Neighbor-scoped MPI_Isend/Irecv

## CFL Deviation Note

The supplied case files specify CFL up to 100. The LU-SGS with simplified
Jacobian is stable up to CFL~5 (inviscid) and CFL~2 (viscous). The run script
caps CFL accordingly. This is a documented conservative deviation.

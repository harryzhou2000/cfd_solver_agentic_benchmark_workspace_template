# fv2d — 2-D unstructured compressible Navier–Stokes solver (MPI)

`fv2d` is a C++17, MPI-parallel cell-centered finite-volume solver for the
two-dimensional compressible Navier–Stokes equations of a calorically perfect
gas, written from scratch for the CFD solver agentic benchmark.

## Features

- Cell-centered unstructured FV on mixed triangle/quad CGNS meshes (multi-zone
  meshes merged through tolerance-based node unification; 1-to-1 zone
  interfaces become interior faces automatically).
- Conservative inviscid flux: HLLC approximate Riemann solver (sub/transonic)
  or Rusanov/LLF (supersonic; selected by Mach number, overridable with
  `--flux`).
- Laminar viscous flux: Newtonian stress + Fourier heat conduction with
  corrected face gradients (over-relaxed formulation); constant viscosity
  matched to the case Reynolds number.
- Second-order piecewise-linear reconstruction (weighted least-squares
  gradients) with Barth–Jespersen limiting, positivity fallback, and a
  shock-aware frozen-limiter steady-convergence policy.
- Implicit time integration: pseudo-time backward Euler with LU-SGS
  relaxation and local CFL-based time steps (steady cases), and BDF2 physical
  time integration with a true two-level loop (outer physical steps, inner
  nonlinear/LU-SGS iterations) for transient cases.
- MPI domain decomposition: METIS k-way graph partitioning on rank 0,
  scattered rank-local meshes (owned + one ghost layer), neighbor-scoped
  `MPI_Isend/Irecv` halo exchanges, global reductions for residuals/forces.
- Boundary conditions: farfield (weak, characteristic-consistent through the
  Riemann solver), inviscid slip wall, viscous no-slip adiabatic wall.

## Dependencies

- Linux, CMake ≥ 3.16, C++17 compiler, MPI (OpenMPI/MPICH).
- CGNS (+HDF5), METIS from the benchmark external tree
  (`external/cfd_externals/install`).
- Header-only nlohmann_json (`external/nlohmann`).
- Python 3 + numpy + matplotlib (post-processing only, in a local `.venv`).

## Build

```bash
cd solver
mkdir -p build && cd build
cmake .. -DCFD_EXTERNALS_ROOT=/workspace/external/cfd_externals/install \
         -DCFD_HEADER_ONLY_ROOT=/workspace/external
make -j8
```

## Python environment (post-processing)

```bash
cd solver
python3 -m venv .venv
.venv/bin/pip install numpy matplotlib
```

## Run

```bash
mpirun -np 8 solver/build/fv2d solve \
  --case <case.json> --output <output-dir> [--restart <restart.bin>] \
  [--flux hllc|rusanov] [--cfl-max <v>] [--max-steps <n>] \
  [--inner-cfl <v>] [--inner-sweeps <n>] [--final-time <t>] [--report-level full]
```

The same executable and command form works for every supplied case; case
differences come only from the JSON input files plus the documented CLI
options above.

Example (production settings used in the submitted results, see
`report/run_manifest.csv`):

```bash
mpirun -np 8  build/fv2d solve --case inputs/cases/naca0012_m080_inviscid.json \
  --output results/naca0012_m080_inviscid
mpirun -np 16 build/fv2d solve --case inputs/cases/cylinder_m010_laminar_re200.json \
  --inner-cfl 10 --inner-sweeps 2 --output results/cylinder_m010_laminar_re200
```

## Outputs

Each run writes the full benchmark contract set: `metadata.json`,
`partition_diagnostics.csv/.json`, `residuals.csv`, `forces.csv`,
`surface.csv`, `field_final.vtk`, `restart_final.bin`, `stdout.log`,
`run_status.json`.

## Post-processing and report

```bash
.venv/bin/python tools/plot_all.py results report          # figures + manifest
.venv/bin/python tools/sanity_checks.py results report     # sanity_checks.json
python3 <benchmark>/examiner/validate_outputs.py results/* --report report
```

`report/report.tex` builds the PDF report with `pdflatex`.

## Layout

```
solver/
  CMakeLists.txt
  src/         # C++ solver sources (mesh/partition/flux/solver/output)
  tools/       # python post-processing + run orchestration
  results/     # final case result directories
  report/      # LaTeX report, figures, manifests
```

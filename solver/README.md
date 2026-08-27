# fv2d - 2-D unstructured finite-volume compressible Navier-Stokes solver

MPI-parallel cell-centered finite-volume solver for the compressible
Navier-Stokes equations of a calorically perfect gas, built for the
cfd_solver_agentic_benchmark cases.

## Dependencies

- C++17 compiler, MPI (OpenMPI/MPICH), CMake >= 3.16
- DNDSR-style externals: CGNS + HDF5 + METIS under
  external/cfd_externals/install (override with -DCFD_EXTERNALS_ROOT=<path>
  or the CFD_EXTERNALS_ROOT environment variable); header-only
  nlohmann/json under external/ (override with -DCFD_HEADER_ROOT=<path>).
- Python 3 with numpy and matplotlib for plotting/validation, in a local
  virtual environment (see below).

## Build

    cd solver
    cmake -B build -DCMAKE_BUILD_TYPE=Release (externals roots are auto-detected)
    cmake --build build -j

The executable is build/fv2d.

## Running

    mpirun -np <ranks> build/fv2d solve --case <case.json> --output <output-dir>
        [--restart <restart-file>] [--report-level brief|full] [--flux roe|rusanov]

One command works for every supplied case; all case differences come from the
JSON input. To run every required case with production settings:

    .venv/bin/python tools/run_all_cases.py --np 8

## Python environment (plotting / validation / report automation)

    cd solver
    python3 -m venv .venv
    ./.venv/bin/pip install numpy matplotlib

Use .venv/bin/python for every script:

    .venv/bin/python tools/plot_case.py --case-dir results/<case> --figdir report/figures
    .venv/bin/python tools/make_figure_manifest.py report/figures
    .venv/bin/python tools/sanity_check.py results

## Layout

- src/ - solver source (CGNS mesh import, METIS partitioning, halo exchange,
  LSQ reconstruction + limiters, Riemann fluxes, LU-SGS implicit solve,
  BDF2 dual-time transient, output writers).
- tools/ - Python orchestration/plotting/validation plus small C++ test
  utilities (test_jac Jacobian finite-difference check, probe_mesh).
- results/<case_id>/ - per-case output directories satisfying
  OUTPUT_CONTRACT.md (metadata.json, residuals.csv, forces.csv, surface.csv,
  field_final.vtk, restart_final.bin, stdout.log, run_status.json,
  partition_diagnostics.csv; the transient case also has fields/field_t*.vtk).
- report/ - LaTeX report, figures, run manifest, sanity checks.

## Numerical method summary

- Cell-centered FV, weighted least-squares linear reconstruction with a
  Barth-Jespersen limiter (Venkatakrishnan available), positivity floors on
  density/pressure and relative update clipping.
- Rusanov (LLF) and Roe (Harten entropy fix) Riemann fluxes; production steady
  cases use Roe; the Re 200 transient uses Rusanov with dissipation scale 1.0
  as specified in the case file.
- Characteristic farfield BC; pressure-flux slip wall; mirrored-state no-slip
  adiabatic wall with corrected-average face gradients for viscous terms.
- Steady marching: local pseudo time steps with the case CFL ramp; inner solve
  by LU-SGS sweep pairs with split flux Jacobians (analytic, verified against
  finite differences in tools/test_jac.cpp). A residual-triggered first-order
  startup phase improves robustness; see the report.
- Transient: BDF2 with frozen physical-time histories inside the inner
  nonlinear loop (LU-SGS relaxations, dual-time local steps).
- MPI: METIS k-way cell-graph partitioning, neighbor-scoped Isend/Irecv halo
  exchange (state, gradients+limiters, corrections), MPI reductions for
  residuals/forces. No full-state or full-mesh replication during iterations.

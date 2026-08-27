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

Use .venv/bin/python for every script. The full post-processing and report
pipeline is:

    # per-case figures (residual/forces/surface_cp/mach/pressure/vorticity)
    .venv/bin/python tools/plot_case.py --case-dir results/<case> --out-dir report/figures
    # figure manifest mapping every figure to its source file/variable
    .venv/bin/python tools/make_figure_manifest.py --figures-dir report/figures --out report/figure_manifest.csv
    # physics sanity gate -> report/sanity_checks.json
    .venv/bin/python tools/sanity_check.py --results-root results --out report/sanity_checks.json --figures-dir report/figures --manifest report/figure_manifest.csv
    # run manifest + per-case summary + Re200 Strouhal analysis
    .venv/bin/python tools/build_report_data.py --results-root results --report-dir report
    # MPI rank-count consistency/timing comparison
    .venv/bin/python tools/run_rank_comparison.py --case <case.json> --tag <label> --steps 1500 --ranks 1 2 4 8
    # regenerate the report results/tables from data and compile the PDF
    .venv/bin/python tools/fill_report.py --report-dir report --analysis-json report/analysis.json
    cd report && pdflatex report.tex

Utility C++ tools: tools/test_jac.cpp (Jacobian finite-difference check),
tools/probe_mesh.cpp (CGNS structure), tools/sym_check.cpp (mesh reflection
symmetry), tools/wall_check.cpp (near-wall first-cell height).

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

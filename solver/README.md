# cfd2d — 2-D unstructured compressible Navier--Stokes solver

cfd2d is a cell-centered finite-volume solver for the 2-D compressible
Navier--Stokes equations on unstructured meshes (triangles/quads), written in
C++17 with MPI + METIS domain decomposition. It implements:

- Roe approximate Riemann solver (Harten entropy fix) and Rusanov flux
- Unweighted least-squares gradients (exact for linear fields, verified by
  the gradcheck command), Barth--Jespersen and Venkatakrishnan limiters
- Corrected-average viscous face gradients; no-slip adiabatic walls via a
  mirrored-ghost wall gradient; characteristic farfield boundary condition
- Implicit steady pseudo-time marching: backward Euler in pseudo time with an
  LU-SGS/SGS defect-correction inner solve (symmetric Gauss--Seidel sweep
  pairs on a simplified Rusanov Jacobian, iterated to the requested inner
  residual reduction)
- Transient BDF2 with a true two-level loop: physical time steps outside,
  defect-correction nonlinear inner iterations inside; BDF2 history
  (U^n, U^{n-1}) frozen during inner iterations and updated only after inner
  convergence; BDF1 first step; linear-extrapolation predictor
- CGNS mesh input (multi-zone with geometric zone merge), METIS k-way
  partitioning, per-rank partition cache, neighbor-scoped halo exchange
  (MPI_Isend/Irecv) of conservative states, gradients, and limiters
- Restart files are global-id ordered and rank-count independent

## Dependencies

- C++17 compiler, CMake >= 3.16, MPI (OpenMPI 4.1 tested)
- CGNS and METIS from the DNDSR externals tree; the CMake cache variable
  CFD_EXTERNALS_ROOT points to the install prefix (in this workspace it
  defaults via the external symlink to /opt/external/cfd_externals/install)
- nlohmann/json headers under /opt/external

## Build

    cd solver
    cmake -S . -B build
    cmake --build build -j8

## Run

    mpirun -np 8 ./build/cfd2d solve +      --case /workspace/cfd_solver_agentic_benchmark/inputs/cases/<case_id>.json +      --output results/<case_id> --limiter venkat --flux roe

Useful options: --restart <file>, --limiter venkat|barth|none,
--flux roe|rusanov, --cfl-max, --residual-target, --max-steps,
--final-time, --pseudo-cfl, --sweeps-per-inner, and
--pert-aoa-deg / --pert-duration (startup symmetry-breaking perturbation
used for the Re 200 case). Diagnostic commands: meshinfo, selftest,
gradcheck (LSQ linear-field exactness), surface (regenerate surface.csv
from a restart file without re-solving).

tools/run_case.sh <case_id> wraps the production invocation (NP and LIMITER
environment overrides).

## Post-processing

    .venv/bin/python tools/plot_results.py      # all figures + figure manifest
    .venv/bin/python tools/sanity_checks.py     # physics/contract sanity checks

Results live in results/<case_id>/; report sources and figures in report/.

# CFD Solver

This directory contains the C++17/MPI finite-volume solver and the reproducible
reporting workflow for the supplied CGNS cases.  The solver reads every case
through its JSON file; no source edits are needed between cases.

## Dependencies

- CMake 3.20 or newer and a C++17 compiler
- MPI with `mpirun`/`mpiexec`
- CGNS and METIS from the benchmark external prefix
- Python 3 for report generation; Python packages are listed in
  `requirements.txt`

The build accepts the external prefix through `CFD_EXTERNALS_ROOT`.  From the
workspace root, configure and build with:

```bash
cmake -S solver -B solver/build \
  -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install" \
  -DBUILD_TESTING=ON
cmake --build solver/build -j
ctest --test-dir solver/build --output-on-failure
```

The CTest suite includes parser/physics/output/operator tests, direct CGNS
imports of both supplied meshes, two-rank distributed-mesh construction,
neighbor halo exchange, and collective propagation of partition failures.

## Solver architecture and extension points

The implementation is split into checked case parsing (`CaseConfig`), CGNS
geometry construction (`Mesh`), METIS partition distribution
(`DistributedMesh`), neighbor communication (`HaloExchange`), perfect-gas and
flux kernels (`Physics`), least-squares/Barth reconstruction
(`Reconstruction`), finite-volume assembly (`SpatialOperator`), implicit
steady/BDF2 marching (`Solver`), and rank-safe output (`Output`).  This keeps
mesh topology, thermodynamics, reconstruction, fluxes, time integration, and
I/O behind separate interfaces rather than embedding the eight supplied cases
in a monolithic loop.

The current conserved state is the four-component two-dimensional perfect-gas
state.  A future 3-D extension would add the third momentum component and 3-D
face geometry in the mesh/physics layers; general equations of state can
replace the conversion functions in `Physics`; and RANS or multispecies state
and flux terms can be introduced without changing METIS partition ownership or
the typed neighbor-exchange pattern.  Those models are extension points, not
claims of functionality in this submission.

Production spatial fluxes use Rusanov/local Lax--Friedrichs, Newtonian stress
and Fourier heat flux for laminar cases, least-squares piecewise-linear
reconstruction with a Barth--Jespersen limiter, and density/pressure positivity
fallback.  The implicit update uses a scalar spectral Jacobian with distributed
additive-Schwarz LU--SGS sweeps.  The Re200 case uses BDF2 (backward-Euler
startup) with frozen physical histories during each inner solve.

## Run a case

All ranks use the same command and write one rank-local field/restart file per
rank plus rank-zero CSV/JSON histories:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json \
  --output solver/results/naca0012_m080_inviscid_np8 \
  --report-level full
```

The required CLI is:

```text
mpirun -np <ranks> solver/build/cfd_solver solve --case <case-json> --output <output-dir>
    [--restart <restart-directory-or-{rank}-path>] [--report-level brief|full]
```

Restart files are partition-specific.  To restart with the same rank count,
pass the previous output directory:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m080_inviscid.json \
  --output solver/results/naca0012_m080_restart \
  --restart solver/results/naca0012_m080_inviscid_np8
```

## Validate outputs and generate the report

Validate completed case directories before submission:

```bash
python3 cfd_solver_agentic_benchmark/examiner/validate_outputs.py solver/results/<case-dir>
```

Create a local virtual environment and regenerate report artifacts from the
actual outputs:

```bash
cd solver
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python tools/generate_report.py --results results --report report --compile-pdf
```

Run the examiner with `--report report` after the report and all required case
directories are complete.

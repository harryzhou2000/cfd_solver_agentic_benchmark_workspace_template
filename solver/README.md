# SaturnCFD

SaturnCFD is an original C++17/MPI, cell-centred finite-volume solver for the
2-D compressible Euler and laminar Navier--Stokes equations.  It is the solver
submission for `cfd_solver_agentic_benchmark`; the benchmark submodule itself
is treated as read-only input.

## Implemented method

- CGNS import of multi-zone unstructured TRI/QUAD meshes, including explicit
  donor-point union at abutting zone interfaces and boundary-family mapping.
- METIS cell-graph partitioning on rank 0 followed by distribution of only
  owned cells, one-ring ghosts, incident faces, and neighbour send/receive
  maps.  The global mesh and the vector of all partitions are released before
  solver iterations start.
- Neighbour-scoped nonblocking state and reconstruction halo exchange.  Global
  collectives are limited to norms, forces, and small diagnostics.
- Conservative Rusanov (local Lax--Friedrichs) steady flux and HLLC transient
  flux with a Rusanov positivity fallback, characteristic farfield, direct
  slip-wall flux, and direct no-slip adiabatic wall treatment.
- Weighted least-squares primitive gradients, active piecewise-linear
  reconstruction, Barth--Jespersen limiting, face positivity scaling, and a
  conservative-update positivity line search.
- Constant Reynolds-matched viscosity, Newtonian stress, Fourier heat flux,
  corrected face-normal gradients, and tangential skin-friction force output.
- CFL-ramped steady pseudo-time march with a distributed 4-by-4 block-Jacobi
  implicit defect correction using normal Euler Jacobian blocks.
- Frozen-history BDF2 (BDF1 startup) with a genuine physical-time outer loop,
  nonlinear inner loop, 4-by-4 block-Newton corrections, and convergence on
  the full spatial-plus-physical-time residual.
- Parallel VTK XML fields, restart, exact CSV schemas, measured partition
  diagnostics, metadata, and run status.

The source separates case parsing, mesh topology, partitioning, gas/transport
physics, residual/time integration, and output.  This leaves explicit seams
for future 3-D geometry, EOS, RANS, and species equation systems without
case-name branches in the solver.

## Dependencies

- CMake 3.20 or newer and a C++17 compiler
- MPI
- CGNS 4.x
- METIS 5.x
- nlohmann/json
- Python 3 with NumPy and Matplotlib for report automation
- `pdflatex` for the optional compiled report

The benchmark-provided dependencies are used from
`external/cfd_externals/install` and `external/`.  Nothing is vendored.

## Build and test

From the parent repository root:

```bash
cmake -S solver -B solver/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install" \
  -DCFD_HEADER_ROOT="$PWD/external"
cmake --build solver/build -j
ctest --test-dir solver/build --output-on-failure
```

Both dependency roots are cache variables and may point elsewhere on another
Linux installation.  The executable is `solver/build/saturn_cfd`.

## Inspect and solve

Mesh import can be checked without solving:

```bash
solver/build/saturn_cfd inspect \
  --case cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json
```

The required solve interface is:

```bash
mpirun -np 8 solver/build/saturn_cfd solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output solver/results/naca0012_m015_inviscid \
  --report-level full
```

Every supplied JSON uses the same executable and command.  `--restart` accepts
the generated `restart_final.json` manifest and requires the same MPI rank
count because restart pieces are rank-local:

```bash
mpirun -np 8 solver/build/saturn_cfd solve \
  --case CASE.json --output NEW_OUTPUT \
  --restart OLD_OUTPUT/restart_final.json
```

The options `--debug-max-steps` and `--debug-final-time` deliberately produce
an incomplete status unless the actual steady convergence gate is reached;
they are for smoke tests and must not be used to shorten final production
results.

## Python environment and automation

The benchmark requires local Python tooling under `solver/.venv`.  Create it
without modifying the system environment:

```bash
python3 -m venv --system-site-packages solver/.venv
solver/.venv/bin/python -c 'import numpy, matplotlib'
```

Production orchestration and report regeneration are then:

```bash
solver/.venv/bin/python solver/tools/run_cases.py --mode production
solver/.venv/bin/python solver/tools/run_cases.py --mode rank-validation
solver/.venv/bin/python solver/tools/generate_report.py
```

`run_cases.py` never fabricates or edits solver outputs.  It launches the exact
CLI, stops on nonzero status, and records the command already emitted by the
solver.  `generate_report.py` reads only submitted CSV/VTK/JSON artifacts,
writes the figure and run manifests plus machine-readable sanity checks, and
runs `pdflatex` when available.

## Output notes

Each result directory contains the contract files.  `field_final.pvtu` is the
parallel VTK master and `field_final_rankNNNN.vtu` contains owned cells for one
rank.  Transient time snapshots use `field_stepNNNNNNNN.*`.  Surface velocities
are boundary values: exactly zero on no-slip walls and tangential projections
on slip walls.  Viscous force columns contain tangential wall shear only;
pressure force remains separate.

Normal final runs return zero only for `converged` or
`statistically_periodic`.  Initialization errors, numerical failures, debug
limits, or missed completion gates return nonzero and are written honestly as
incomplete artifacts.

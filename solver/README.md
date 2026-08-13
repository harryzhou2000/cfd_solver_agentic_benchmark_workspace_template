# AeroFV: reproducible 2-D compressible-flow benchmark solver

AeroFV is an original C++20/MPI, cell-centred finite-volume solver for the
supplied 2-D unstructured NACA0012 and cylinder meshes.  It is designed to run
the benchmark cases directly from their JSON inputs, produce the complete
machine-readable result package, and generate the accompanying LaTeX report.
Python is used only for orchestration, post-processing, reporting, and
validation.

The benchmark inputs and acceptance criteria remain authoritative:

- [task specification](TASK.md), [case reference](CASES_REFERENCE.md), and
  [production numerical controls](NUMERICAL_PARAMETERS.md);
- [case-input schema](INPUT_FORMAT.md) and [output contract](OUTPUT_CONTRACT.md);
- [report requirements](report_requirements.tex) and the
  [transparent validator](examiner/validate_outputs.py).

Do not modify files below `inputs/`.  A completed result is one produced by an
actual solver run and marked `converged` (steady) or `statistically_periodic`
(the Re200 transient); diagnostic or interrupted directories are not final
results.

## Requirements

The build requires a C++20 compiler, CMake 3.20 or newer, an MPI C++ toolchain,
CGNS, and METIS.  The default CMake search finds system installations.  In a
DNDSR-style environment, point `CFD_EXTERNALS_ROOT` at the dependency prefix
that contains `include/` and `lib/`.  The implementation also uses Boost
PropertyTree headers, which are commonly supplied with the system Boost
development package.

Report compilation additionally needs a LaTeX installation with `latexmk`.
All Python tools use the local virtual environment and require Python 3.10+,
NumPy, and Matplotlib.

From the repository root, create that environment once:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -r requirements.txt
```

## Configure, build, and test

The following is the normal system-library build.  Add `-DCMAKE_PREFIX_PATH`
only when CGNS/METIS are installed outside the standard search path.

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For a supplied external dependency prefix, configure instead (or in addition):

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFD_EXTERNALS_ROOT=/path/to/external/cfd_externals/install
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The test suite covers strict case parsing, gas/flux and boundary-condition
behaviour, manufactured second-order reconstruction, CGNS topology, METIS
partition/distribution, and MPI halo exchange.  The MPI tests launch four
ranks; ensure the chosen MPI launcher permits local multi-rank jobs.

## Solver CLI and restart

```bash
build/aerofv --help
mpirun --bind-to core --map-by slot -np 8 build/aerofv solve \
  --case inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid \
  --report-level full
```

The exact interface is:

```text
aerofv solve --case <case-json> --output <output-dir>
             [--restart <restart-file>] [--report-level brief|full]
```

`--case` and `--output` are required.  The executable deliberately refuses a
nonempty output directory, which prevents stale files being mixed with a new
solution.  Choose a new directory, or use the all-case runner's scoped
`--force` option when replacing an incomplete run.

Every successful run writes `restart_final.bin`.  It stores the four
conservative variables keyed by global cell ID and can be read on a different
MPI rank count, for the same mesh, for example:

```bash
mpirun --bind-to core --map-by slot -np 4 build/aerofv solve \
  --case inputs/cases/naca0012_m015_inviscid.json \
  --restart results/naca0012_m015_inviscid/restart_final.bin \
  --output results/naca0012_m015_restart_np4
```

A restart supplies the initial state; it does not preserve the previous run's
iteration counter, output history, or transient BDF2 history.  The invoked
case's complete run controls therefore apply again.

## Production case campaign

Run all eight required cases and the default MPI comparison set from a clean
`results/` directory:

```bash
.venv/bin/python tools/run_cases.py \
  --executable build/aerofv --results results --ranks 8 --concurrent 1
```

The runner records exact commands, rank counts, wall time, and status in
`report/run_manifest.csv`.  By default it additionally runs the NACA M0.15
inviscid and cylinder Re20 cases at 1, 2, and 4 ranks under
`results/rank_comparisons/`.  This supplies the required rank-count evidence.
Use `--no-rank-comparisons` only for a diagnostic campaign; use `--cases` to
select named cases; and use `--force` only to remove and rerun existing output
directories beneath the selected results root.

The runner limits simultaneous MPI launches with `--concurrent` (default 2).
Choose it so that `concurrent × ranks` fits the allocated CPU cores and memory;
one concurrent job is the conservative, reproducible choice on a workstation.
Its driver logs live in `results/_driver_logs/`, outside solver-owned case
directories.

### Runtime expectations

These are production CFD runs, not short smoke tests.  The steady case files
request 20,000--50,000 pseudo-time steps with CFL ramps and convergence checks.
The cylinder Re200 case is especially expensive: it advances exactly 30,000
physical steps (`dt=0.01` to `t=300`) and permits 5--1000 inner iterations per
step.  Runtime depends strongly on hardware, rank count, and nonlinear
convergence; plan an unattended, multi-hour (and potentially much longer)
campaign.  Do not call a shortened run a final result.  The supplied
[numerical controls](NUMERICAL_PARAMETERS.md) explain the required horizon and
the conditions for an earlier steady stop.

## Numerical and MPI design

Each rank retains only its owned cells plus a one-layer ghost region.  Rank zero
reads CGNS and constructs the cell graph; METIS k-way partitions it, and compact
rank-local meshes are distributed before solving.  State, gradient, and limiter
halos use nonblocking neighbor-scoped `MPI_Isend`/`MPI_Irecv`; no full mesh or
full state is replicated during iterations.  `partition_diagnostics.csv`
records owned/ghost sizes, neighbor counts, and send/receive counts.

The solver advances the calorically perfect, compressible 2-D
Navier--Stokes equations with:

- Rusanov/local-Lax--Friedrichs inviscid face fluxes and characteristic
  farfield, slip-wall, or no-slip adiabatic-wall treatments;
- corrected central viscous fluxes from primitive-variable gradients, Newtonian
  stress, and Fourier heat flux in laminar cases;
- weighted least-squares piecewise-linear reconstruction with active
  Barth--Jespersen limiting and face/update positivity controls;
- matrix-free GMRES pseudo-transient Newton steps for steady cases;
- a true dual-time physical loop for Re200, initialized from a converged
  second-order steady base flow: backward-Euler startup followed by a
  positivity-limited extrapolated predictor and frozen-history BDF2 inner
  solves.

Steady cases use a first-order CFL-continuation start before blended
least-squares reconstruction is enabled; the Re200 base-flow initializer uses
the same continuation, while every reported physical-time state uses the full
second-order reconstruction. This is recorded in each run's `metadata.json`.
Method names and observed inner-solve statistics are written from the actual
run rather than inferred by the reporting tools.

## Output layout

For a completed case directory, AeroFV produces:

```text
results/<case-id>/
  metadata.json                 method, MPI, mesh, and completion metadata
  run_status.json               solver argv, timing, final status
  partition_diagnostics.csv     per-rank ownership and halo statistics
  residuals.csv                 global conservative residual history
  forces.csv                    pressure/viscous force split and coefficients
  surface.csv                   boundary-state wall quantities and Cp/Cf
  field_final.vtu               final unstructured field (ParaView-readable)
  restart_final.bin             global-ID keyed conservative-state restart
  stdout.log                    solver progress log
  field_t*.vtu                  transient snapshots when requested by the case
```

The final field contains density, pressure, Mach, temperature, velocity,
velocity magnitude, vorticity, total energy, owner rank, and global cell ID.
The final force row, surface data, field, and restart are written from the same
final state.  See the [output contract](OUTPUT_CONTRACT.md) for exact CSV/JSON
schemas and submission requirements.

## Post-process, report, and validate

After all production case directories exist and completed successfully, create
figures, statistics, and physics sanity checks:

```bash
.venv/bin/python tools/postprocess.py --results results --report report
```

This creates `report/figures/`, `report/figure_manifest.csv`,
`report/result_statistics.json`, and `report/sanity_checks.json` from the
submitted CSV and VTU files.  It fails if a required physical sanity check does
not pass rather than manufacturing a successful result.

Then generate and compile the data-driven LaTeX report:

```bash
.venv/bin/python tools/generate_report.py --strict
./tools/build_report.sh
```

Finally validate every required output directory plus the report:

```bash
python3 examiner/validate_outputs.py \
  results/naca0012_m015_inviscid \
  results/naca0012_m080_inviscid \
  results/naca0012_m200_inviscid \
  results/naca0012_m015_laminar_re5000 \
  results/naca0012_m080_laminar_re5000 \
  results/naca0012_m200_laminar_re5000 \
  results/cylinder_m010_laminar_re20 \
  results/cylinder_m010_laminar_re200 \
  --report report
```

The validator checks structure and basic consistency; it is necessary but not
sufficient.  Inspect the residual and force histories, field figures, wall
quantities, MPI comparisons, and the resulting PDF before submission.  The
report generator intentionally shows missing evidence rather than silently
inventing it, so run it only after the corresponding solver artifacts have been
generated for a final submission.

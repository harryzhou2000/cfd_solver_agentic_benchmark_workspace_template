# Aurora-FV: 2-D MPI unstructured compressible-flow solver

The C++17 cell-centred finite-volume solver is in `src/main.cpp`; it reads
unstructured CGNS TRI/QUAD zones, pairs multi-zone interfaces, partitions the
cell graph with METIS, exchanges neighbor halos with nonblocking MPI, and
writes the benchmark output contract.  Reconstruction is least-squares
piecewise-linear with a Barth--Jespersen positivity limiter and no-slip
wall-image samples in the gradient stencil; inviscid fluxes are Rusanov
(local Lax--Friedrichs).  The low-Mach Re=200 transient uses an acoustic-scaled,
all-speed Rusanov signal: the supplied Rusanov scale remains `1.0`, while a
cutoff of `0.4` limits only its artificial acoustic contribution.  This is not
a full pressure--velocity preconditioned flux.  Laminar fluxes include Newtonian
stress/Fourier conduction, and the steady pseudo-time diagonal uses
face-normal distances for anisotropic boundary cells so the physical wall
traction is resolved consistently in the residual and force outputs.
Steady runs use local-CFL implicit scalar Jacobi/Richardson relaxation with an
MPI-global Armijo backtracking safeguard on each accepted correction.  The Re=200
cylinder uses that same scalar Jacobi/Richardson nonlinear inner solver in a
true 30,000-step BDF2 dual-time outer loop with frozen history states and five
or more inner iterations.

MPI startup is a serial preprocessing phase on rank zero: the CGNS mesh is
read once, METIS assigns the cell graph, and compact temporary partition files
are written under `.partition_cache/`.  Each rank then loads only its owned
cells, one-ring ghosts, and incident faces; neighbor-only nonblocking exchanges
update conservative states and reconstructed gradients.  The preprocessing
mesh is released before iterations and is reread on rank zero only for final
field/restart output.

Dependencies are the supplied MPI toolchain plus CGNS, METIS, and nlohmann-json
under `external/cfd_externals/install` and `external/nlohmann`.

Build:

```bash
cmake -S . -B build -DCFD_EXTERNALS_ROOT="$PWD/../external/cfd_externals/install" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run one case (the same command works for every supplied JSON):

```bash
mpirun -np 2 build/aurora-fv solve \
  --case ../cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output results/naca0012_m015_inviscid --report-level full
```

The output directory contains `metadata.json`, partition diagnostics, residual
and force histories, boundary-state `surface.csv`, legacy VTK field data, and
an ASCII restart file.

For bounded diagnostics, `--max-steps`, `--cfl-max`, `--steady-omega`, and
`--steady-block-coupling B` override the corresponding run controls and are
recorded in metadata.  `B` is constrained to `[0,1]` and enables an optional
frozen local 4x4 block-Jacobi correction on top of the scalar steady
spectral-radius backbone; the default is zero (the scalar path).  The
steady solver normally runs until its requested residual reduction or its
supplied step budget.  `--steady-plateau-stop` is an explicit short-diagnostic
opt-in that permits early exit after a stable force/residual plateau.
transient-only `--transient-slope`, `--transient-gain`, `--transient-seed`,
`--transient-cutoff`, `--transient-block-coupling`, `--inner-target`, and
`--rusanov-scale` switches are
likewise diagnostic overrides; `--restart PATH` and `--resume-step N` resume a
transient (using the optional BDF2 history sidecar when present), and
`--report-level brief|full` selects report verbosity.
`--no-wall-init` disables the optional wall-compatible initializer.  Short
runs, altered inner targets, and other override runs are diagnostics only and
must not be relabeled as the final Re=200 submission.  The current diagnostic
candidate uses a fresh startup perturbation of `0.001`, a transient
reconstruction-slope factor of `0.1`, scalar correction gain `0.8`, the supplied
Rusanov dissipation scale of `1.0`, and an all-speed acoustic cutoff of `0.4`.
These are override-study controls, not a claim that the checked-in historical
Re=200 package reached a statistically periodic state; that package records
its own gain/cutoff values in `metadata.json`.
The cutoff scales only the artificial acoustic part of the transient Rusanov
signal; it does not change the conservative physical flux or the supplied
Rusanov-scale control.  Fresh transient runs start from freestream plus a
one-time localized phase seed; the no-slip wall is imposed by the physical
wall flux and gradient treatment, not by a pre-projected stationary wall
profile.  These controls are recorded in final metadata/report.  If a BDF2
nonlinear stall occurs, the
solver retries the same discrete residual with bounded pseudo-time damping,
then records a time-consistent backward-Euler fallback with 2, 4, 8, 16, 32,
64, or 128 substeps totaling exactly one nominal `dt`; every accepted fallback
substep must meet the inner target.  Histories advance only after an accepted
BDF2 or BE interval.  After an accepted fallback, the next interval starts
from the last accepted state and permits the ordinary BDF2 extrapolated
predictor before retrying the normal residual; the fallback does not force an
additional first-order physical interval.  Retry, substep, fallback, and one-step recovery counts are
recorded in metadata.  A transient restart requires both
`--restart PATH` and `--resume-step N`.  Each transient run writes
`restart_final.history.dat` beside `restart_final.dat`; when that optional
sidecar is present, the exact accepted U^{n-1} history is restored and the
resumed interval can use BDF2 directly.  Legacy state-only restarts without
the sidecar reconstruct the missing history with one BE interval.  An
explicitly supplied transient seed may be applied once to a legacy restarted
initial state to select a wake phase; it is not persistent forcing.  Any
best-effort target-miss continuation is recorded as diagnostic-only and is not
eligible for a final statistically-periodic submission.
A case may additionally set
`run_control.rusanov_dissipation_scale` (default 1) for a documented numerical
study.
A global ASCII `restart_final.dat` can be loaded with `--restart PATH`.

Create the local plotting environment (not committed):

```bash
python3 -m venv .venv
.venv/bin/pip install matplotlib numpy
```

Run supplied cases after building the executable:

```bash
.venv/bin/python tools/run_cases.py --solver build/aurora-fv --mpi 2
.venv/bin/python tools/postprocess.py results
.venv/bin/python tools/generate_report.py results report
```

`run_cases.py` defaults to `mpirun -np N <solver> solve --case CASE --output DIR`.
Use `--command-template` when the solver CLI differs.  For example,
`--command-template '{solver} {case} {output}'` disables the MPI launcher.
The final command and return code are recorded in `report/run_manifest.csv`.
The report and validation helpers select only the eight case IDs supplied by
the benchmark; preserved audit/probe directories are ignored.

The postprocessor accepts ASCII legacy VTK (`field_final.vtk`) and CSV fields
with `x,y` and named scalar columns.  It produces only figures traceable to a
field or CSV source, and `generate_report.py` writes an honest sanity JSON and
a LaTeX report from the available results.

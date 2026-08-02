# AsteriaFV solver and report workflow

The C++17 solver reads the benchmark CGNS meshes, partitions with METIS, and
runs under MPI. Python is used only to inspect already-written solver output and
render a report; it does not generate solver histories or upgrade a failed run.

Steady runs preserve the supplied startup CFL and ramp duration while using a
documented conservative terminal cap. Anderson history is reset as the CFL
changes, accelerated states must reduce both global L2 and Linf residuals, and
completion requires both residual targets together with a stable force tail.

For steady inviscid flow, uniform freestream total enthalpy is enforced as the
adiabatic-Euler invariant during nonlinear updates and at reconstructed face
states. The Rusanov energy flux is the upwind total enthalpy times its numerical
mass flux, so the constrained path is compatible with the conservative spatial
residual. Face extrapolations also use a documented joint anti-vacuum safeguard:
when reconstructed density and pressure would both cross 10% of their
freestream references, that face alone falls back to its admissible cell-center
state. A smooth pre-emptive sensor is active only on cells with compactness
`4A/sum(edge_length^2) < 0.1`: as both density and pressure fall from 25% toward
10% of freestream, it continuously flattens the face increment, raises shared
Rusanov jump dissipation from 1x to at most 2x, and reduces local pseudo-time
CFL by at most two. The same numerical flux is applied with opposite signs to
its two cells, so the stabilization remains conservative and does not clamp the
state. These constraints are deliberately disabled for laminar and transient
cases.

## Build and tests

From the repository root, configure with the supplied external dependency prefix:

```bash
cmake -S solver -B solver/build \
  -DCFD_EXTERNALS_ROOT="$PWD/external/cfd_externals/install" \
  -DCFD_BUILD_TESTING=ON
cmake --build solver/build -j
ctest --test-dir solver/build --output-on-failure
```

Create a repository-local Python environment for report tooling:

```bash
cd solver
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m pytest -q tests
cd ..
```

## Production runs and restart

Run each case without diagnostic overrides. The command recorded by the solver
contains only the executable arguments, so keep the corresponding `mpirun` line
in your launch log as well.

For long runs on a shared machine, `tools/launch_pinned_case.sh` records the
exact `mpirun` command, pins each rank to a consecutive CPU, and writes sibling
`.launch.log`, `.launcher.pid`, and `.launcher.status` evidence. A user service
keeps the solve independent of the invoking terminal; use one production
service at a time when host resource pressure is high:

```bash
stage=$(mktemp -d solver/results/.naca0012_m015_inviscid.staging.XXXXXX)
systemd-run --user --unit=asteria-naca-m015i \
  --property=WorkingDirectory="$PWD" \
  /usr/bin/env -u CODEX_THREAD_ID -u CODEX_CI -u CODEX_PERMISSION_PROFILE \
  solver/tools/launch_pinned_case.sh 8 0 \
  cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  "$stage"
```

The launcher refuses non-empty outputs or pre-existing evidence files. Promote
a staging directory only after its status, examiner result, force tail, and
field sanity checks all pass.

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --output solver/results/naca0012_m015_inviscid --report-level full

mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json \
  --output solver/results/cylinder_m010_laminar_re20 --report-level full
```

To restart from a compatible final state, use the completed result's restart
file and a new output directory:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json \
  --restart solver/results/naca0012_m015_inviscid/restart_final.bin \
  --output solver/results/naca0012_m015_inviscid_restart --report-level full
```

A same-case restart is not perturbed. For the Re200 production run, the
converged Re20 field is used as a cross-case precursor and the documented wake
seed is explicitly reapplied. The physical step remains exactly `0.01`, the
final time remains `300`, every physical step must meet the `1e-3` total BDF2
residual target, and the supplied fixed pseudo-time CFL of 1 is retained:

```bash
mpirun -np 8 solver/build/cfd_solver solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  --restart solver/results/cylinder_m010_laminar_re20/restart_final.bin \
  --restart-perturbation true \
  --output solver/results/cylinder_m010_laminar_re200 --report-level full
```

The transient solver atomically replaces `transient_checkpoint.bin` every 100
accepted physical steps.  Unlike the state-only `restart_final.bin`, this
checkpoint contains both accepted BDF2 states, the original residual baseline,
the complete inner-iteration accounting, the initial wake-seed provenance, and
the force history needed for an uninterrupted statistical analysis.  After an
interruption, resume into the same unfinished output directory; rows newer than
the durable checkpoint are validated and trimmed before cadence-1 output
continues:

```bash
solver/tools/launch_pinned_case.sh 8 0 \
  cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  solver/results/cylinder_m010_laminar_re200 \
  --resume solver/results/cylinder_m010_laminar_re200/transient_checkpoint.bin
```

`--restart` and `--resume` are mutually exclusive.  A resume is accepted only
for the same transient case, mesh size, physical time step, and unfinished
history package; the launcher records each continuation in distinct hashed
evidence files.

For the long Re200 production package, use the restart-safe supervisor under a
user service with `Restart=on-failure`.  It records the initial launch before
starting it, takes an advisory per-output lock, and subsequently permits only a
matching unfinished package with a durable checkpoint.  The service exits
successfully (without relaunching MPI) when it finds a validated completed or
explicitly failed `run_status.json`; it also refuses if a live `cfd_solver`
already advertises that output directory.

```bash
systemd-run --user --unit=asteria-cylinder-re200 \
  --property=WorkingDirectory="$PWD" \
  --property=Restart=on-failure \
  --property=RestartSec=15 \
  --property='RestartPreventExitStatus=64 66 69 73' \
  /usr/bin/env -u CODEX_THREAD_ID -u CODEX_CI -u CODEX_PERMISSION_PROFILE \
  solver/tools/supervise_transient_case.sh 8 0 \
  cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  solver/results/cylinder_m010_laminar_re200 \
  --restart solver/results/cylinder_m010_laminar_re20/restart_final.bin \
  --restart-perturbation true
```

Do not remove the sibling `.transient-supervisor.state` while a run is active;
it prevents an interrupted initial attempt from being launched twice and pins
the case, initial options, rank/CPU allocation, and solver-binary hash across
all continuation segments.  Exit status 75 alone is treated as retryable;
systemd suppresses retries for usage, missing-input, safety, and lock refusals.
The `.transient-supervisor.lock` file may remain after a clean stop, but the
actual `flock` is released automatically when its supervisor exits.

For rank validation, retain independently completed directories for at least one
NACA case and one cylinder case at two rank counts including `np=8`, for example
`solver/rank_validation/naca0012_m015_inviscid_np1` and `_np8`. Do not overwrite
the production directories used for the eight required cases.

## Report generation

The renderer accepts ASCII legacy VTK `field_final.vtk` or ASCII XML
`field_final.vtu`. Fields must contain density/rho, pressure/p, Mach/mach, and,
for Re200, a two-component velocity array or scalar `u`/`v`. It verifies final
force time/step consistency, steady history sufficiency, the exact Re200
production time horizon/cadence, and rank-count evidence. These are consistency
checks, not proof of physical correctness or run provenance.

```bash
cd solver
.venv/bin/python tools/build_report.py --results results --output ../report \
  --rank-results rank_validation/naca0012_m015_inviscid_np1 \
                 rank_validation/naca0012_m015_inviscid_np8 \
                 rank_validation/cylinder_m010_laminar_re20_np1 \
                 rank_validation/cylinder_m010_laminar_re20_np8
cd ../report
latexmk -pdf -interaction=nonstopmode -halt-on-error report.tex
```

The report directory contains `report.tex`, `run_manifest.csv` (production and
rank-validation records), `sanity_checks.json`, `figure_manifest.csv`, and
`figures/`. The Re200 spectrum figure and its caption record the force sampling,
window, spectral resolution, dominant frequency, and peak-quality ratio.

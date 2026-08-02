# AsteriaFV solver and report workflow

The C++17 solver reads the benchmark CGNS meshes, partitions with METIS, and
runs under MPI. Python is used only to inspect already-written solver output and
render a report; it does not generate solver histories or upgrade a failed run.

Steady runs preserve the supplied startup CFL and ramp duration while using a
documented conservative terminal cap. Anderson history is reset as the CFL
changes, accelerated states must reduce both global L2 and Linf residuals, and
completion requires both residual targets together with a stable force tail.

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

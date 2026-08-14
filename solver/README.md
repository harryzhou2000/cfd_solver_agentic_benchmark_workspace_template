# CFD solver

`cfd_solver` is a distributed-memory, cell-centred 2-D compressible
Navier--Stokes solver. Rank 0 reads the case and CGNS mesh, METIS partitions the
cell graph, and rank-local owned/one-ring-ghost meshes are sent to all ranks.
Iterations exchange only scheduled neighbour halos; full state gathering is
limited to startup/restart, requested samples, and final output.

## Dependencies and clean build

The build requires CMake 3.20+, a C++17 compiler, MPI, CGNS, METIS, and
nlohmann/json. Install the non-system dependencies under one prefix and build
from a clean directory:

```bash
rm -rf build
cmake -S . -B build \
  -DCFD_EXTERNALS_ROOT=/path/to/cfd-externals \
  -DBENCHMARK_ROOT=/path/to/cfd_solver_agentic_benchmark \
  -DBUILD_TESTING=ON
cmake --build build -j
```

Python is not used by the solver. For benchmark orchestration and
postprocessing, create a local environment:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r tools/requirements.txt
```

## CLI

```bash
mpirun -np 8 build/cfd_solver solve \
  --case /path/to/case.json --output results/case \
  [--restart results/older/restart_final.bin] [--report-level brief|full]
```

Diagnostic-only `--max-steps` (steady) and `--final-time` (transient) overrides
are available. Production runs should omit them; when used they are recorded in
`metadata.json` and force `completed=false`. `--progress-every` and
`--flush-every` tune operational cadence without changing the case physics.

The steady method first attempts every implicit pseudo-time update with
right-preconditioned matrix-free GMRES and a frozen first-order full-block
Rusanov LU-SGS preconditioner. The operator retains every conservative 4x4
interior-face coupling, configured Rusanov dissipation, local wall/farfield
blocks, and the pseudo-time spectral diagonal. At least three ascending/descending
global-cell-ID sweeps are used; cross-rank corrections are block-lagged and
exchanged between half-sweeps, and the reported defect is the true MPI-global
frozen linear defect. Steady initialization uses a generic spatial-order continuation:
the inviscid face state starts cell-centred (reconstruction blend zero), is held
for `min(500, max(1, pseudo_cfl_ramp_steps/4))` accepted steps, and then follows
a smooth ramp over exactly the same bounded number of accepted steps. Blend one
is enabled only after every startup and ramp update has been accepted; rejected
attempts never advance either counter. Viscous gradients remain active throughout.
The CFL is reset at transition onset, and every residual evaluation in JFNK,
line search, fallback, caching, forces, and logging uses the same active blend.
Repeated JFNK globalization failures or 16-step residual stagnation enable a
focused implicit trust-region retry for the active continuation operator. At full
order, after the mandatory ordinary JFNK attempt fails, the retry evaluates every
decade CFL strictly between the current CFL and configured `cfl_max` (for example,
`0.1`, `1`, and `10` between `0.01` and `100`). Each candidate uses matrix-free
JFNK, the same full-block LU-SGS preconditioner, GMRES tolerance `1e-2`, restart
50, at most 50 iterations, positivity globalization, and line search down to
`2^-20`. The solver evaluates all candidates and accepts only the finite candidate
with the lowest actual residual when that residual strictly decreases from the
common initial full-order residual. Failed batches retain the state and enter a
persisted 16-attempt cooldown. The existing endpoint rescue then remains available
at `cfl_max`, with its persisted 32-attempt cooldown. Neither path can promote
spatial order, and both cooldowns suppress only their secondary path: ordinary
JFNK with full-block LU-SGS still runs on every steady attempt.
Every ordinary, trust-region, and endpoint-rescue matrix-free action uses the
same residual-adaptive centered-difference multiplier
`clamp(sqrt(current_global_L2 / active_phase_baseline), 1e-3, 1)`. At full
order, `active_phase_baseline` is the persisted initial full-order residual;
during startup and ramp phases it is the persisted initial residual of the
current reconstruction-blend phase. Missing or invalid scales conservatively
select one. This changes only the Jacobian sampling radius: positivity halving,
line search, default strict actual-residual acceptance, and convergence gates are
unchanged. Logs, metadata, and restart continuation record the actual latest
multiplier, reference residual, absolute epsilon, and positivity halvings.
Every retry batch is logged with per-candidate CFL, GMRES, line-search, update,
and component-residual diagnostics. Continuation state preserves cumulative
batch/candidate/iteration counts and the most recent successful candidate's CFL,
GMRES iterations, line scale, and residual before/after; later failed batches do
not erase that accepted-candidate evidence.

At any fixed spatial-order phase, three consecutive implicit attempts without a
numerically meaningful strict global-best decrease enable a bounded implicit
pseudo-transient bridge. This prevents a converged continuation operator from
rejecting forever before its required accepted-step hold is complete; advancing
the reconstruction blend resets the bridge and its entry cap for the new
operator. The superseded nonmonotone envelope remains readable in old restarts
but is disabled in production flow. After strict JFNK, trust-region, and rescue
paths fail, the bridge solves
`(sigma/CFL I + J_frozen) delta = -R` with the existing full-block Rusanov
LU-SGS operator. It applies the largest globally positivity-safe geometric scale
and does not require per-step residual decrease, but every accepted residual must
remain at or below `1.25 * bridge_cycle_entry`. A mildly growing physical
pseudo-time trajectory that reaches this cap at the CFL floor starts a new
bounded cycle from its current accepted residual rather than rejecting the same
state forever. CFL starts at 0.1 within case
bounds, halves after more than 5% residual growth, and grows by 20% after a
decrease. A meaningful new global best is required in every 500 accepted bridge
steps, and at most 500 bridge steps are accepted. Restart and metadata preserve
bridge CFL, residual range/growth, best improvements, and watchdog stops. The
diagnostic `U_best` state is never restored or used for final output or
convergence.

The same full-block LU-SGS operator can provide a standalone implicit
pseudo-transient recovery direction at CFL 0.1 or lower for at most 64
consecutive steps, including at full order after higher-priority JFNK paths fail.
Positivity and an actual active-operator residual
decrease are required by a line search down to `2^-20`; no divergent full-order
diagonal trajectory is accepted. Fallback is only a secondary path after a
failed ordinary JFNK attempt. Fallback mode requires accepted residual history,
and its mode, window, and consecutive-step count reset whenever the
reconstruction blend changes so restart state never mixes operators. `U_best` remains
diagnostic state; it is never restored into the accepted trajectory. Restart
continuation and metadata retain rescue attempts/accepts/GMRES statistics,
LU-SGS application/sweep/defect statistics,
fallback disable state, CFL/window state, spatial-order phase, and cumulative
method counts. Steady completion requires blend exactly one and at least
`max(50, min(250, pseudo_cfl_ramp_steps / 10))` accepted full-order implicit
updates; rejected attempts do not count. The residual gate is exactly
`current_global_l2 <= original_run_global_initial_l2 *
10^(-residual_reduction_target)`, followed by the existing positivity and
physics gates. The original global MPI residual is preserved across spatial
continuation and restart and is the `residual_reduction_orders` reporting
baseline required by `OUTPUT_CONTRACT.md`. The residual at entry to full order
is preserved separately as a diagnostic only and produces
`full_order_residual_reduction_orders`. Final residuals, forces, surfaces, and
completion gates always use blend one. Spatial
reconstruction uses weighted least-squares primitive
gradients and the smooth, scale-aware Venkatakrishnan face limiter. Local strong
density/pressure jumps switch to a stricter Barth--Jespersen limiter;
inadmissible faces retry that limiter before positivity scaling and first-order
fallback. Metadata reports these final-state limiter/fallback counts separately.

Transient cases remain full second order and use a physical-time BDF1 startup
and BDF2 thereafter. Their dual-time nonlinear corrections use matrix-free
GMRES with the same frozen full-block Rusanov LU-SGS preconditioner as steady
flow, augmented by the exact BDF physical diagonal. The two BDF histories are
frozen during each nonlinear inner loop and advance only after acceptance. A
run exits nonzero unless the requested steady reduction is
reached, or a transient reaches its exact final time with strict accepted-step
inner convergence, at least 95% target convergence, and a documented
post-transient force-periodicity test.

## Outputs and restart

The output directory contains `metadata.json`, `run_status.json`, streamed
`residuals.csv` and `forces.csv`, per-rank `partition_diagnostics.csv`, boundary
values in `surface.csv`, an actual-cell XML `field_final.vtu`,
`restart_final.bin`, and `stdout.log`. CSV schemas follow `OUTPUT_CONTRACT.md`.
The surface `cf` is signed tangential body shear divided by local face length
and freestream dynamic pressure; it is zero for inviscid walls. No-slip output
velocity is exactly zero, while slip output removes only normal velocity.

Restart files use binary v15 (`CFDRST15`): fingerprint, case ID, exact running
executable SHA-256, global cell count, accepted step, physical time, opaque
continuation bytes, then records containing an `int64` global cell ID and 16
doubles (current state, both BDF histories, and diagnostic best state).
Continuation state includes the CFL state, spatial-order blend and accepted-step
counts, trust-region batch/candidate/acceptance diagnostics and cooldown,
Newton-rescue and fallback-disable state, original-run and diagnostic full-order
residual baselines,
adaptive JFNK epsilon diagnostics,
legacy nonmonotone migration fields, bounded implicit pseudo-transient bridge
state and diagnostics,
convergence statistics, bounded
periodicity window, original start time, stream evidence, and cumulative
attempted-step count. The reader migrates v8 (`CFDRST8\0`), v9 (`CFDRST9\0`),
v10 (`CFDRST10`), v11 (`CFDRST11`), v12 (`CFDRST12`), v13
(`CFDRST13`), and v14 (`CFDRST14`) checkpoints. Missing diagnostics receive safe defaults;
for v10 and older, the overloaded active-phase residual scale is replaced by a
fresh global evaluation of the original uniform initial state while the
persisted full-order scale remains diagnostic. Only rank 0 reads
and validates a restart; it then
distributes values for each rank's owned and ghost IDs.

The solver atomically publishes `<output>/restart_checkpoint.bin` every 500
steady attempts or 100 accepted transient steps, on rejected transient
steps, and at shutdown. Resume that same output in place with:

```bash
mpirun -np 8 build/cfd_solver solve --case case.json --output results/case \
  --restart results/case/restart_checkpoint.bin --resume-output
```

An exact-control production resume validates the case, mesh, executable, and
stream history directly from the checkpoint, so it also works after an abrupt
job timeout that occurred before final metadata was published. Resume metadata
is additionally required only when extending a diagnostic `--max-steps` budget.

For an incomplete capped steady diagnostic, `--max-steps` may be increased on
resume. The solver validates the prior budget and case fingerprint from
`metadata.json`, permits no lower budget or other case change, reads the old
checkpoint fingerprint, and publishes subsequent checkpoints with the extended
budget fingerprint.

In-place resume validates and appends existing CSV/log streams without duplicate
headers or steps. It removes an incomplete trailing line and complete CSV rows
newer than the checkpoint's last-recorded-step evidence, then verifies row
counts and last steps before appending. A restart into a new output directory is
deliberately diagnostic/incomplete because prior CSV history is absent.

CSV rows are buffered and checked after writes, at configurable flush cadence,
and before checkpoints/publication; the solver retains only a 4096-sample force
window rather than the full transient history. Existing halo exchange already
uses neighbor-only messages and bounded per-neighbor request buffers. Those
buffers were not redesigned in Phase 3 because changing their lifetime would
risk message aliasing; their allocation scales with neighbor count, never the
30,000-step history or global state.

Metadata records the parent git revision and dirty state captured at every
build, SHA-256 of the running executable, case fingerprint, mesh fingerprint,
periodicity measurements, and common final physics-gate measurements.

## Tests and benchmark workflow

```bash
ctest --test-dir build --output-on-failure
mpirun -np 8 build/output_tests
```

The all-eight benchmark suite entry point is expected to be:

```bash
.venv/bin/python tools/run_suite.py \
  --solver build/cfd_solver \
  --cases-dir /path/to/cfd_solver_agentic_benchmark/inputs/cases \
  --results-dir results --ranks 8
```

Postprocessing is intentionally external. The expected placeholder invocation
for a future project wrapper is:

```bash
.venv/bin/python tools/postprocess.py --output results/case  # placeholder; not shipped
```

Diagnostic smoke runs and overridden runs are not production convergence
claims.

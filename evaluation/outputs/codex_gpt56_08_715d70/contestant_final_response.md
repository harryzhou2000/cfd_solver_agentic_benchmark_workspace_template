## Objective

Complete all eight benchmark cases, especially a credible Re=200 vortex-street transient. Do not mark the existing flat-lift 30,000-step Re=200 result complete.

## Current source changes

Uncommitted edits are in [`solver/src/main.cpp`](/workspace/solver/src/main.cpp):

- Exact HLLC now activates the finite-difference face-block path whenever `hllcFluxBlend > 0`, instead of excluding exact HLLC.
- `update_owned` now receives frozen gradients and builds HLLC Jacobians from reconstructed face states, matching `assemble`.
- Full-slope HLLC runs use an explicit physical-step continuation:
  `slope = min(requested_slope, 0.10 + 0.0009 * step)`, reaching 1.0 at step 1000.
- Metadata records requested slope, active final slope, and the continuation schedule.
- MPI runs now respect explicit `OMP_NUM_THREADS`; without it they retain one thread per rank.
- `<cstdlib>` was added for `getenv`.

Build succeeds:

```bash
cmake --build solver/build_current -j2
```

## Validation evidence

- Existing seven non-Re200 cases plus report pass validation.
- [`solver/diag_hllc_facestate005_5`](/workspace/solver/diag_hllc_facestate005_5):
  - 5 steps, full requested slope, 0 target misses
  - 0 BE fallback steps
  - final lift range `0.00563356`
- [`solver/diag_hllc_schedule20`](/workspace/solver/diag_hllc_schedule20):
  - 20 steps, strict fraction 1.0
  - 0 target misses, 0 BE fallback steps
  - lift range `0.0154866`
  - active slope at step 20: `0.118`
- [`solver/diag_hllc_schedule20_gain1`](/workspace/solver/diag_hllc_schedule20_gain1):
  - 20 strict steps, no fallback
  - lower average inner iterations (`149.4` vs `200.9`), but poor gain-1 startup retries
- Historical archive [`solver/probe_hllc_dual_schedule_100`](/workspace/solver/probe_hllc_dual_schedule_100) reached 100 strict steps, slope `0.19`, lift range `0.01024`, with no BE fallback. It took about 293 seconds on four MPI ranks.

## Active process

An OpenMP performance test is still running:

```bash
env -u LD_LIBRARY_PATH OMP_NUM_THREADS=4 \
  /usr/bin/mpirun --allow-run-as-root -np 4 \
  solver/build_current/aurora-fv solve \
  --case cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  --output solver/diag_hllc_schedule20_omp4 \
  --max-steps 20 --transient-gain .5 --transient-seed .001 \
  --transient-cutoff .1 --inner-target .001 --rusanov-scale 1 \
  --inviscid-flux hllc --transient-linesearch off
```

Session ID: `16041`. Poll it before starting another run.

## Remaining work

1. Finish/poll the OpenMP test and compare wall time.
2. Run schedule tests to 100, 200, then near step 1000 using a persistent/background execution method; ordinary tool sessions tend to terminate around a minute.
3. Determine whether full slope remains stable after the ramp reaches 1.0.
4. Optimize enough to make the 30,000-step production run practical.
5. Preserve honest metadata: early ramped steps are not full-slope; document the continuation explicitly.
6. Only after Re200 reaches `t=300`, has ≥95% strict inner convergence, and shows meaningful late lift oscillation (>1e-5), regenerate results/report/figures/manifest/sanity checks and run the all-eight validator.

## Constraints

- Do not modify benchmark input files.
- Do not use synthetic force histories or strong artificial forcing.
- Do not relabel existing flat-lift Re200 outputs as complete.
- Use `apply_patch` for edits.
- Use CodeGraph before source searches; already done this turn.
- MPI probes generally require escalation.
- `.git` is effectively read-only; do not push/pull/switch branches or rely on commits.
- Preserve the many diagnostic artifacts already in the workspace.

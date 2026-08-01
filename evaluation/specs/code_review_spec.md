# Code Review Specification

## Purpose

Define **common reviewable points** that every contestant submission is scored
on, independent of which model or agent produced it, so code quality is
comparable across runs.

## Method

- The machine-readable point list lives in
  `evaluation/config/review_points_code.json` (single source of truth for the
  scorecard generator).
- Reviewers receive `review_code.md`, score each point 0–5, and mark any
  disqualification flags they observe.
- Overall score = weighted mean of point scores (weights in the config).

## Points

1. **code.build — Build system and reproducibility** (0.15): clean documented
   build from a clean checkout; configurable `CFD_EXTERNALS_ROOT`; no absolute
   machine paths; no manual case edits.
2. **code.cli — CLI and error handling** (0.10): required
   `solve --case <json> --output <dir>` form works for every case; bad inputs
   exit nonzero with clear messages.
3. **code.organization — Organization and extensibility** (0.12): clear module
   boundaries; non-case-specific physics interfaces; extensible BCs;
   3-D-ready geometry abstractions; structure supports EOS/RANS/species
   extension.
4. **code.originality — Originality and license compliance** (0.15): solver
   core, time integration, and MPI communication original; no copied or
   wrapped open-source CFD solver; no calls to existing solver executables.
5. **code.hardcoding — No hard-coded case logic** (0.08): cases are driven by
   JSON inputs; no mesh-filename or case-id branches.
6. **code.mpi — MPI correctness** (0.15): neighbor halo exchange; global
   reductions; no full-state allgather/full-mesh replication during
   iterations; rank-independent output; honest `halo_exchange` metadata.
7. **code.robustness — Robustness and memory sanity** (0.08): bounds/error
   checks; no per-rank full-mesh storage during the solve; handles mixed
   cells and degenerate geometry.
8. **code.testing — Testing and validation evidence** (0.10): unit tests where
   practical; smoke runs; serial-vs-parallel comparisons; validator run.
9. **code.hygiene — Code hygiene** (0.07): readable naming, comments on
   numerics, no dead/debug code or scratch files in tracked source.

## Disqualification flags

From `TASK.md` / `SCORING_RUBRIC.md` (any one may invalidate the submission):
existing solver executable called internally; results generated without
solving; hard-coded mesh-filename branches; order-one rank-count changes for
steady cases; explicit time stepping only; report claims absent from source;
failed runs marked successful; report figures not matching outputs; METIS
claimed but geometric/full-replication used; claimed limiter disabled in
production; mislabeled wall rows; copied solver core.

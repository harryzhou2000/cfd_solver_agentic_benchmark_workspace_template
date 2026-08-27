# Agent Evaluation Report — opus46-01

- Evaluated at: 2026-08-27T14:41:22.618230+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-01` branch `codex/generic/opus46-01` commit `f6796449524c8b7e5f17c021cdc92a5a4194b17a`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-24T14:03:41.157000+00:00 → 2026-08-24T16:13:38.448000+00:00
- Time: goal 7790.0s, wall 7797.3s
- Tokens: 78,666,333 (main 73,040,715 / subagents 5,625,618); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 14 threads / opencode 0 sessions
- Window: 2026-08-24T14:03:41.157000+00:00 → 2026-08-24T16:13:38.448000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 78,666,333, cache hit 0.9938, tools 579

### Structural result checks (auto)

- Required cases: 8; found: 11; missing: none
- Figure manifest: {"manifest_present": true, "entries": 41, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "surface.csv", "field_final.vtk", "field_final.vtk", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtk", "field_final.vtk", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtk", "field_final.vtk", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtk"
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re20_np8`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid_np8`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `verify_test`: missing=['surface.csv', 'field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=True status=converged

## Methodology

Read source, report manifest, raw result/status packages, and final response. The explicit validator passes all eight packages and report. Substantive review found report/status disagreement for M2 cases, Re20 drag about 7.23, and Re200 drag about 4.85 with near-zero lift. No solver rerun was performed.

`run_identity.json` SHA-256: `e75eccd7679bb5bd0980f8a169be57952393f4b6f96e7082d9f5ec105a34137e`; `contestant_final_response.md` SHA-256: `e385551c3147d91112c4cfd0aad324d25eba1191470bc598b9ec799f2dbfd922`. These are unindexed sidecars not validated by `cfdeval check`.

## Rubric scores (100 points, SCORING_RUBRIC.md)

| Section | Max | Score | Notes |
|---------|----:|------:|-------|
| Build, CLI, Output Contract | 10 |  |  |
| Mesh And Geometry | 10 |  |  |
| Finite-Volume Residual And Boundary Conditions | 15 |  |  |
| Second-Order Spatial Scheme | 10 |  |  |
| Viscous Terms | 10 |  |  |
| Implicit And Transient Methods | 15 |  |  |
| MPI | 10 |  |  |
| Case Results And Validation | 10 |  |  |
| Report, Visualization, Analysis | 5 |  |  |
| Extensibility | 5 |  |  |

## Review-area scores (0-5 weighted, review_*.json)

| Area | Overall | Notes |
|------|--------:|-------|
| Code |  |  |
| CFD methods |  |  |
| Results |  |  |

## Per-case scores (independent 0-5)

These scores do not contribute to the review-area scores or the 100-point rubric total.

| Case | Score | Notes |
|------|------:|-------|
| NACA0012 M0.15 inviscid |  |  |
| NACA0012 M0.80 inviscid |  |  |
| NACA0012 M2.00 inviscid |  |  |
| NACA0012 M0.15 Re5000 |  |  |
| NACA0012 M0.80 Re5000 |  |  |
| NACA0012 M2.00 Re5000 |  |  |
| Cylinder M0.10 Re20 |  |  |
| Cylinder M0.10 Re200 |  |  |

## Disqualification assessment

DQ=false. No external solver, fake MPI, explicit-only implementation, or other listed trigger was proven. Numerical plausibility defects are scored in their relevant criteria.

## Metadata answers & session selection

Selected workspace-local root `01a0340e-8e50-7490-b64e-976393b0d123` plus 13 descendants as the benchmark session tree.

## Limitations

No independent build/rerun was performed and Re200 was not rerun. Raw packages/PDF are excluded from the curated tip. Figure inspection was representative rather than exhaustive.

## Verdict

72/100, DQ=false. This is structurally complete with substantive solver/MPI evidence, but implausible cylinder physics and report/status inconsistency materially reduce CFD/result credit.

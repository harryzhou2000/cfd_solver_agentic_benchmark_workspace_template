# Agent Evaluation Report — sonnet5-01

- Evaluated at: 2026-08-28T03:10:45.886071+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/sonnet5-01` branch `codex/generic/sonnet5-01` commit `4965edd288059055b7feed0af2192347d23d9333`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-27T03:40:35.437000+00:00 → 2026-08-27T18:13:23.563000+00:00
- Time: goal 52345.0s, wall 52368.1s
- Tokens: 529,822,828 (main 183,417,045 / subagents 346,405,783); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 23 threads / opencode 0 sessions
- Window: 2026-08-27T03:40:35.437000+00:00 → 2026-08-27T18:13:23.563000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 529,822,828, cache hit 0.9727, tools 2607

### Structural result checks (auto)

- Required cases: 8; found: 74; missing: none
- Figure manifest: {"manifest_present": true, "entries": 143, "missing_figures": [], "missing_sources": ["cylinder_m010_laminar_re20/residuals.csv", "cylinder_m010_laminar_re20/forces.csv", "cylinder_m010_laminar_re20/surface.csv", "cylinder_m010_laminar_re20/surface.csv", "cylinder_m010_laminar_re20/field_final.vtu", "cylinder_m010_laminar_re20/field_final.vtu", "cylinder_m010_laminar_re20/field_final.vtu", "cylind
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=['field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=['field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `naca0012_m200_inviscid`: missing=['field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=['field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `cylinder_m010_laminar_re20_np1`: missing=[] completed=False status=not_converged
- `cylinder_m010_laminar_re20_np2`: missing=[] completed=False status=not_converged
- `cylinder_m010_laminar_re20_np4`: missing=[] completed=False status=not_converged
- `cylinder_m010_laminar_re20_np8`: missing=[] completed=False status=not_converged

## Methodology

Reviewed source, selected Codex root, raw workspace packages, terminal response, explicit eight-case/report validator, and the immutable report. All packages validate; the 77-page immutable rendering has pervasive obvious missing-asset placeholders. No solver/Re200 rerun occurred.

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

DQ false: no trigger was affirmatively established from direct internal evidence. The report failure is direct craftsmanship/traceability evidence rather than an extra deduction for the absence record.

## Metadata answers & session selection

Selected root `01a0414d-9e12-7582-ae04-f90553e9f3a1` and its descendants as the continuous benchmark execution tree.

## Limitations

Raw packages are workspace-only evidence after curation. No evaluator rerun was performed.

## Verdict

Solver/result evidence is substantial, but the immutable report cannot serve as a usable visual deliverable because required figure assets are missing from the curated tip.

## Sidecar integrity

- `run_identity.json` SHA-256: `d6d5d2375da2d0f7194486ae31f1e7f700c56e8a702d828e9fe37b78c51cbfb4`.
- `contestant_final_response.md` SHA-256: `9e1debbc756e4a9209227e9ae25197425eb84000c60231292e070a4043e81aa3`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.

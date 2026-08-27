# Agent Evaluation Report — sonnet46-02

- Evaluated at: 2026-08-27T15:13:24.004709+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/sonnet46-02` branch `codex/generic/sonnet46-02` commit `25daaa33082392c46c69425cd9c0673ec04e1b63`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-26T09:59:53.541000+00:00 → 2026-08-27T02:41:15.165000+00:00
- Time: goal 60060.0s, wall 60081.6s
- Tokens: 144,649,877 (main 144,649,877 / subagents 0); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 1 threads / opencode 0 sessions
- Window: 2026-08-26T09:59:53.541000+00:00 → 2026-08-27T02:41:15.165000+00:00
- Idle excluded: 9 gaps, 12661s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 144,649,877, cache hit 0.9646, tools 1412

### Structural result checks (auto)

- Required cases: 9; found: 16; missing: quick_test_600
- Figure manifest: {"manifest_present": true, "entries": 42, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.
- `cylinder_m010_laminar_re20_np8`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=failed
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_backup`: missing=[] completed=True status=failed
- `naca0012_m015_inviscid_np8`: missing=[] completed=True status=plateaued
- `test_frozen20`: missing=[] completed=True status=failed

## Methodology

Read immutable source/report, eight raw final packages, and the sole workspace-local Codex root. Ran the examiner explicitly on all packages plus report; it passed. Audited result tip `25daaa33082392c46c69425cd9c0673ec04e1b63` from checkpoint `113f01aa755d6db31e41b1b50602c7397ee73e71`; 290 prohibited paths were removed from its index. No evaluator rerun was performed, including no Re200 rerun.

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

DQ=true: Trigger 7. The raw M0.15 inviscid metadata/run-status marks the max-step 1.69-order result `completed:true` and `converged`; late force history is not steady and the report itself calls it a plateau below the target. Source accepts any reduction above one order as converged. No other DQ trigger was directly established.

## Metadata answers & session selection

No unanswered metadata questions. Selected the sole main Codex root `01a03d82-f3a0-7493-b962-06cedb6fdd56` from workspace `.sessions`; persisted UTC interval is 2026-08-26T09:59:53.541000+00:00 to 2026-08-27T02:41:15.165000+00:00. The extracted terminal response is backed by bundled rollout evidence.

`run_identity.json` SHA-256 is `0c6331b900139d57f5d2a05d4f01a72510d4ee96056c96194eafb918c0e73894`; `contestant_final_response.md` SHA-256 is `cdabfdf5238bdbc36fd77d3fad45c47c7a77d2e3ed8a2917be12369e007b8725`. Both are unindexed sidecars and are not validated by `cfdeval check`.

## Limitations

No evaluator clean build or MPI rerun. Re200 was not rerun; readable deliverables show BDF2/inner target evidence, but step 30001/time 300.01 and zero reported reduction are limitations.

## Verdict

Score: **80/100; disqualified: true.** The solver/report have substantial delivery value and a structurally passing package, but the false-success M0.15 status is a disqualifying output-integrity failure.

# Agent Evaluation Report — 09

- Evaluated at: 2026-08-27T15:27:37.248121+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/09` branch `codex/gpt56/09` commit `7f22dbe795a59666ef9889686e204163a1ad7b65`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-16T09:40:24.573000+00:00 → 2026-08-16T19:19:01.264000+00:00
- Time: goal 34703.0s, wall 34716.7s
- Tokens: 322,712,982 (main 307,941,708 / subagents 14,771,274); cost est. $39.93

### Session analysis (auto)

- Source: project — codex 5 threads / opencode 0 sessions
- Window: 2026-08-16T09:40:24.573000+00:00 → 2026-08-16T19:19:01.264000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 322,712,982, cache hit 0.9831, tools 2255

### Structural result checks (auto)

- Required cases: 8; found: 46; missing: none
- Figure manifest: {"manifest_present": true, "entries": 49, "missing_figures": [], "missing_sources": []}
- `adaptive_probe1_serial`: missing=[] completed=False status=failed
- `adaptive_probe3`: missing=[] completed=False status=failed
- `audit_naca_slip`: missing=[] completed=True status=converged
- `audit_re20_wall`: missing=[] completed=True status=converged
- `audit_slip_new`: missing=[] completed=True status=converged
- `re200_mpi2_no_fast10`: missing=[] completed=True status=statistically_periodic
- `re200_no_fast100_omega095`: missing=[] completed=True status=statistically_periodic
- `re200_no_fast20_omega095`: missing=[] completed=True status=statistically_periodic
- `re200_no_fast20_omega115`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re200_fast_legacy`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re200_np8_legacy`: missing=[] completed=True status=statistically_periodic
- `late_probe_om04`: missing=[] completed=True status=statistically_periodic

## Methodology

Read immutable source/report, raw packages, and selected workspace-local Codex tree. Explicit examiner invocation passed all eight packages plus report. Result tip audit passed; no evaluator rerun and no Re200 rerun.

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

DQ=true, trigger 7: multiple steady run-status files claim converged despite far-below-target reductions, including Re20 0.876/5 at 30000 steps and M015 inviscid 1.485/4 at 20000. Structural validation is separate and passed.

## Metadata answers & session selection

Selected root `01a009f1-0252-7e80-acd4-2783b82d1076` plus four descendants from workspace-local telemetry. Identity SHA-256 `44949e3d9bc47a941728253d7785f057f8a91bc498bc43a881a240485916cf2c`; final-response SHA-256 `e8a3430eefc6f55656d570df8f0409f736f4bdcf9a62371da496041d2285c556`. Both are unindexed sidecars and are not validated by `cfdeval check`.

## Limitations

No evaluator clean build or MPI rerun. Re200 was not rerun.

## Verdict

Score: **75/100; disqualified: true.** Strong structural package and readable BDF2 evidence do not cure false successful steady statuses.

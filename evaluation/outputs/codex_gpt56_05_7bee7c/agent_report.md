# Agent Evaluation Report — 05

- Evaluated at: 2026-08-15T06:25:26.503591+00:00
- Evaluating agent: codex-evaluator
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/05` branch `codex/gpt56/05` commit `85d470dd16c1a8cae379513d3cbd5ca59f2ce030`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-05T13:29:10.098000+00:00 → 2026-08-05T19:17:15.900000+00:00
- Time: goal 20869.0s, wall 20885.8s
- Tokens: 154,028,422 (main 115,696,883 / subagents 38,331,539); cost est. $91.4499

### Session analysis (auto)

- Source: project — codex 8 threads / opencode 0 sessions
- Window: 2026-08-05T13:29:10.098000+00:00 → 2026-08-05T19:17:15.900000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 154,028,422, cache hit 0.9730, tools 1332

### Session accounting correction

Fork replay previously added 28,098,818 inherited tokens to descendant totals. The selected tree is corrected from 182,127,240 to 154,028,422 owned tokens, and its current-price estimate from $102.1771 to $91.4499. Evaluation evidence, scores, and DQ verdict are unchanged.

### Structural result checks (auto)

- Required cases: 8; found: 1; missing: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_inviscid, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Figure manifest: {"manifest_present": false}
- `debug_re20_sgs`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged

## Methodology

Audited curated commit 85d470dd against immutable attempt 37b8e68. Official validator passed seven steady case directories and report; Re200 official validation is OOM-unverified, not passed or failed. No Re200 rerun.

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

DQ false: source inspection supports a native MPI FV solver; no triggered shortcut is established.

## Metadata answers & session selection

Selected project Codex root 019fd21c-986b-7cb2-adf8-e274cf4b8308. Legacy provenance is post-run reconstructed.

## Limitations

Raw packages are excluded from the curated commit. The 802 MB Re200 residual exceeds official validator memory, so that case remains validation-unverified.

## Verdict

High-quality source/report and seven official validator passes; retain the Re200 validation limitation.

run_identity.json SHA-256: `8afee0d65b287815ad0d37afe012bdadf21ed1f72ba0600424f36b7a79501a9b`; contestant_final_response.md SHA-256: `e2fa92927636989cfc3d81b33f128add4c59c8926c6658eb3ebdb12579a56f69`. These unindexed sidecars are not validated by cfdeval check.

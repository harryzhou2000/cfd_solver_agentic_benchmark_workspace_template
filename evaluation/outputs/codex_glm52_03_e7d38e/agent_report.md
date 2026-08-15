# Agent Evaluation Report — 03

- Evaluated at: 2026-08-15T06:12:23.761057+00:00
- Evaluating agent: codex-evaluator
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/03` branch `codex/glm52/03` commit `9532e19d7552900782e8835c441cbed2303178f5`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-01T16:28:54.124000+00:00 → 2026-08-01T19:59:29.319000+00:00
- Time: goal 10600.0s, wall 12635.2s
- Tokens: 138,043,451 (main 124,783,892 / subagents 13,259,559); cost est. $14.93

### Session analysis (auto)

- Source: project — codex 12 threads / opencode 0 sessions
- Window: 2026-08-01T16:28:54.124000+00:00 → 2026-08-01T19:59:29.319000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 138,043,451, cache hit 0.9886, tools 851

### Structural result checks (auto)

- Required cases: 8; found: 0; missing: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_inviscid, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Figure manifest: {"manifest_present": false}

## Methodology

Immutable curated submission `9532e19d7552900782e8835c441cbed2303178f5` was audited against reconstructed initial `de4960b2c72f0217b8e526539e02badbeebae9ca`. A detached temporary worktree at immutable attempt `0305e4500d5c62cf325680114e794ba1d97a4e9b` was validated explicitly: all eight canonical case directories and report returned OK. Those raw results are evaluator evidence only and were not copied into the submission or snapshot. Selected project-local Codex root is `019fbe25-8c45-7f20-a5e8-9501b5d23ba3`; terminal final response is preserved exactly.

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

DQ: **false**. Source inspection found no internal executable wrapper, explicit-only solver, or demonstrable fake MPI. Trust policy prohibits external copying investigation. The raw attempt metadata and report were structurally valid; Re200 lacks credible vortex shedding but that is scored as an incomplete result, not a DQ.

## Metadata answers & session selection

No metadata questions remain. The sole continuous project-local Codex root and descendants were selected; start date is 2026-08-01 UTC. The legacy env snapshot is a post-run reconstruction with unavailable original runtime.

## Limitations

Curated submission excludes raw output packages, manifests and PDF. The original run environment is unavailable; raw validation is from an immutable temporary attempt worktree. No solver rerun was performed, and Re200 was never rerun.

## Verdict

The attempt structurally validates all eight cases and includes substantive source/report material, but delivered Re200 evidence admits no vortex shedding and the curated submission cannot itself reproduce raw results. The independent score layers record those limitations.

Identity sidecar SHA-256: `25d332395b95e073c4f1a151feeb81b18fb6dc0b81214e208c757c924d573077`; final-response sidecar SHA-256: `035c606e6a20e27d6d06c83d42545ba99a3e7ec232a8ddcf91216cd3632bee7d`. These sidecars are unindexed, and `cfdeval check` validates indexed JSON artifacts rather than them.

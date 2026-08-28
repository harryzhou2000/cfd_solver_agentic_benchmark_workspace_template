# Agent Evaluation Report — opus-09

## Verdict

**90/100; not disqualified.** Explicit validation passed all eight original
packages and the 12-page report. Source inspection found CGNS/METIS,
neighbor-scoped MPI exchange, finite-volume Rusanov flux, limiter, viscous
terms, LU-SGS and BDF2 structure. No case was rerun, especially not Re200.
The case layer is deliberately lower where delivered physical evidence is
weak: M2 laminar reports negative drag and cylinder Re20 Cd is 2.99. The
Re200 package is complete at t=300 and is scored as delivered evidence only.

The selected workspace-local Claude root is
`cfdc5453-1ac8-4229-9859-aa4e2a87c0a8`, UTC 01:41:57--04:55:22 on
2026-08-28, including five persisted subagents. Its terminal end-turn prose
was read before scoring. The reviewed sibling `solver/report/report.pdf` is
the main readable 12-page report and is vendored as manager evidence.

Limitations: no clean evaluator build/MPI rerun, report/data manifests are
curated out of the immutable tip, and `run_identity.json` plus the terminal
response Markdown remain unindexed sidecars.

- Evaluated at: 2026-08-28T06:06:56.348678+00:00
- Evaluating agent: Codex
- Harness: claude

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09` branch `claude/generic/opus-09` commit `7e16370db4546b8e957a612177f850742bf4b6f8`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-28T01:41:57.259000+00:00 → 2026-08-28T04:55:22.374000+00:00
- Time: goal Nones, wall 11605.1s
- Tokens: 50,513,895 (main 49,336,233 / subagents 1,177,662); cost est. $48.70

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-28T01:41:57.259000+00:00 → 2026-08-28T04:55:22.374000+00:00
- Idle excluded: 1 gaps, 603s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 50,513,895, cache hit 0.9363, tools 335

### Structural result checks (auto)

- Required cases: 8; found: 11; missing: none
- Figure manifest: {"manifest_present": true, "entries": 49, "missing_figures": [], "missing_sources": ["results/naca0012_m015_inviscid/residuals.csv", "cl", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m015_inviscid/field_final.vtu", "results/naca0012_m080_inviscid/residuals.csv", "cl", "result
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re20_np8`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np8`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_test`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged

## Methodology

Read-only source, terminal-response, raw-package, report-PDF and explicit validator review; no evaluator build or rerun.

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

No direct internal evidence established any of the 13 disqualification triggers.

## Metadata answers & session selection

The selected root and its persisted Claude subagent tree are recorded above; unavailable metadata remains unavailable.

## Limitations

No clean evaluator build/MPI rerun; Re200 was never rerun.

## Verdict

Complete structural delivery with material physical-plausibility limitations noted in the verdict.

Integrity: `run_identity.json` SHA-256 `e37d67ca3a55c31e3d33f3f0fd1b4fa025ac23aced4cbdee912aaea943e01e22`; `contestant_final_response.md` SHA-256 `e1d176b5fdbc5e4fc87cb54b888df688570b095715c5d32440ddcdb516917d2b`. These unindexed sidecars are not validated by `cfdeval check`.

# Agent Evaluation Report — opus46-02

- Evaluated at: 2026-08-27T14:46:13.141927+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-02` branch `codex/generic/opus46-02` commit `1904149e1cfb41664bf41e7786f614a2ba6ca56e`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-25T01:19:34.641000+00:00 → 2026-08-25T03:33:46.516000+00:00
- Time: goal 7692.0s, wall 8051.9s
- Tokens: 59,332,420 (main 50,124,064 / subagents 9,208,356); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 17 threads / opencode 0 sessions
- Window: 2026-08-25T01:19:34.641000+00:00 → 2026-08-25T03:33:46.516000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 59,332,420, cache hit 0.9832, tools 518

### Structural result checks (auto)

- Required cases: 8; found: 11; missing: none
- Figure manifest: {"manifest_present": true, "entries": 30, "missing_figures": ["figures/cylinder_m010_laminar_re20_cp.png", "figures/cylinder_m010_laminar_re20_forces.png", "figures/cylinder_m010_laminar_re20_mach.png", "figures/cylinder_m010_laminar_re20_pressure.png", "figures/cylinder_m010_laminar_re20_residuals.png", "figures/naca0012_m015_inviscid_cp.png", "figures/naca0012_m015_inviscid_forces.png", "figures
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re20_np8`: missing=['partition_diagnostics.csv', 'partition_diagnostics.json', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np8`: missing=['partition_diagnostics.csv', 'partition_diagnostics.json', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `test_naca_1p`: missing=['partition_diagnostics.csv', 'partition_diagnostics.json', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=False status=failed

## Methodology

Read source, final response, final run manifest and raw packages. Each case directory passes alone, but the explicit complete validator fails because the report figure manifest maps paths as `figures/figures/...`; the final manifest leaves M2 laminar and Re200 pending. No solver rerun was performed.

`run_identity.json` SHA-256: `849ea0290da49190e7fc766dab050317b8bfe21726baa88ef2f5c7408e942269`; `contestant_final_response.md` SHA-256: `fe36d58f081aa1976ffe561f9bafef3c4015b6f0fde783309a7526d92472e497`. These are unindexed sidecars not validated by `cfdeval check`.

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

DQ=false. The broken complete contract and incomplete cases reduce their own criteria; no listed disqualification trigger was proved.

## Metadata answers & session selection

Selected workspace-local root `01a0367d-d349-7061-afc5-967548eecef7` plus 16 descendants as the benchmark tree.

## Limitations

No independent build/rerun was performed and Re200 was not rerun. Raw packages/PDF are excluded from the curated tip. Final-response claims conflict across resumptions.

## Verdict

48/100, DQ=false. Source and individual package work is substantial, but the complete report contract is broken and the final manifest explicitly leaves M2 laminar/Re200 unfinished.

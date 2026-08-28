# Agent Evaluation Report — sonnet46-01

- Evaluated at: 2026-08-28T03:04:11.227464+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/sonnet46-01` branch `codex/generic/sonnet46-01` commit `058801fbb4c962f3ce9a8207ef24049b5c2ca54f`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-26T09:58:12.716000+00:00 → 2026-08-28T01:31:20.094000+00:00
- Time: goal 98016.0s, wall 142387.4s
- Tokens: 361,569,173 (main 228,682,378 / subagents 132,886,795); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 11 threads / opencode 0 sessions
- Window: 2026-08-26T09:58:12.716000+00:00 → 2026-08-27T14:33:54.890000+00:00
- Idle excluded: 2 gaps, 10951s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 1 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 361,569,173, cache hit 0.9626, tools 4189

### Structural result checks (auto)

- Required cases: 8; found: 12; missing: none
- Figure manifest: {"manifest_present": true, "entries": 26, "missing_figures": [], "missing_sources": ["field_final.pvtu", "field_final.pvtu", "residuals.csv", "field_final.pvtu", "field_final.pvtu", "residuals.csv", "field_final.pvtu", "field_final.pvtu", "residuals.csv", "field_final.pvtu", "field_final.pvtu", "residuals.csv", "field_final.pvtu", "field_final.pvtu", "residuals.csv", "field_final.pvtu", "field_fin
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re20_np8`: missing=['surface.csv', 'field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=failed
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_tmp`: missing=[] completed=True status=failed
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000_backup`: missing=['partition_diagnostics.csv', 'partition_diagnostics.json', 'surface.csv', 'field_final.vtu', 'restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=True status=converged
- `naca0012_m200_tmp`: missing=[] completed=True status=failed

## Methodology

Reviewed source, selected Codex root, contestant final response, raw workspace packages, and immutable report. The 11-page report rebuilt and all pages rendered. No solver or Re200 rerun occurred.

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

DQ false: no trigger was affirmatively established from direct internal evidence. Extensive raw/build artifacts were index-curated and recorded as hygiene/traceability limitations.

## Metadata answers & session selection

Selected Codex root `01a03d80-e7ad-7301-a090-e5beafdecdc7` and its descendants as the continuous benchmark tree.

## Limitations

No independent solver rerun occurred. Raw cases remain workspace evidence only after curation.

## Verdict

Substantive source/report evidence exists, but lower result credit reflects documented weak M2-laminar and Re200 case evidence plus massive artifact curation.

## Sidecar integrity

- `run_identity.json` SHA-256: `346ccf0725272765f9470f5123347eac9f0f483f76ad3f9a062e9f4e98fdd129`.
- `contestant_final_response.md` SHA-256: `f129ebc157d4e75c36231820fa5c30e342af3eb72542d19f10b3b55400e0eb80`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.

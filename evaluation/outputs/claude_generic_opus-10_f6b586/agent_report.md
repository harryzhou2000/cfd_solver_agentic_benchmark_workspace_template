# Agent Evaluation Report — opus-10

## Verdict

**99/100; not disqualified.** Explicit validation passed all eight original
packages and the 32-page report. Submitted source shows CGNS multi-zone mesh
handling, METIS partitioning, neighbor MPI exchange, Roe/Harten--Yee plus
Rusanov transient flux, Venkat limiting, viscous terms, LU-SGS, and BDF2
dual-time integration. Delivered Re200 reaches t=300 with periodic evidence;
it was never rerun. The one-point case-result deduction preserves the report's
honest M2 plateau/Re200-dissipation limitation.

The selected workspace-local Claude root is
`54c28431-7c49-49b5-898e-22000924875f`, UTC 01:43:36--04:41:41 on
2026-08-28, with four persisted subagents. Its terminal end-turn response was
read. The readable sibling 32-page main report PDF was reviewed and vendored
as manager evidence.

Limitations: no clean evaluator build/MPI rerun; raw packages and generated
PDF/manifests are excluded only at the immutable result tip; identity and
terminal-response Markdown sidecars are unindexed.

- Evaluated at: 2026-08-28T06:06:56.412467+00:00
- Evaluating agent: Codex
- Harness: claude

## Authoritative scorecards

### Review areas

| Area | Weighted score | Evidence |
|---|---:|---|
| Code | 4.50/5 | CMake, modular source, CLI and delivered METIS/MPI evidence. |
| CFD | 4.50/5 | CGNS/FV/Roe/Venkat/viscous/LU-SGS/BDF2 source and package evidence. |
| Results | 4.50/5 | Eight packages validate, report/PDF and rank study are readable. |

### 100-point rubric

| Section | Score | Evidence |
|---|---:|---|
| Build, CLI, Output Contract | 10/10 | Documented source/CLI and all schemas validate. |
| Mesh And Geometry | 10/10 | CGNS multi-zone geometry/graph source reviewed. |
| Finite-Volume Residual And Boundary Conditions | 15/15 | FV/Roe/wall/farfield source paths present. |
| Second-Order Spatial Scheme | 10/10 | Reconstruction, Venkat limiter and positivity source. |
| Viscous Terms | 10/10 | Gradient Newton--Fourier and wall outputs. |
| Implicit And Transient Methods | 15/15 | LU-SGS/BDF2 plus t=300 and inner evidence. |
| MPI | 10/10 | METIS/neighbor exchange/reductions and rank evidence. |
| Case Results And Validation | 9/10 | All validate; M2 plateau/Re200 underprediction disclosed. |
| Report, Visualization, Analysis | 5/5 | Readable 32-page report with equations, fields and MPI analysis. |
| Extensibility | 5/5 | Separated generic solver interfaces. |

### Independent case results

| Case | Score | Evidence |
|---|---:|---|
| M0.15 inviscid | 4.5/5 | Validator-passing converged package. |
| M0.80 inviscid | 4.5/5 | Validator-passing converged package. |
| M2 inviscid | 4/5 | Delivered bounded plateau, candidly disclosed. |
| M0.15 laminar Re5000 | 4.5/5 | Validator-passing wall/friction evidence. |
| M0.80 laminar Re5000 | 4.5/5 | Validator-passing wall/friction evidence. |
| M2 laminar Re5000 | 4/5 | Delivered plateau limitation. |
| Cylinder Re20 | 4.5/5 | Validator-passing steady wake/rank evidence. |
| Cylinder Re200 | 4/5 | Delivered t=300 periodic package; dissipation limit disclosed. |

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-10` branch `claude/generic/opus-10` commit `d25cd3cb7c1c75e75812670fd717eca3b61d68ce`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00
- Time: goal unavailable (Claude has no goal timer), wall 10685.5s
- Tokens: 121,129,727 (main 119,862,076 / subagents 1,267,651); cost est. $76.37

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-28T01:43:36.307000+00:00 → 2026-08-28T04:41:41.816000+00:00
- Idle excluded: 0 gaps, 0s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 121,129,727, cache hit 0.9933, tools 396

### Structural result checks (auto)

- Required cases: 8; found: 12; missing: none
- Figure manifest: {"manifest_present": true, "entries": 57, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "field_final.vtu", "field_final.vtu", "residuals.csv", "forces.csv", "surface.csv", "surface.csv", "f
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20_np1`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20_np2`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np1`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_np2`: missing=[] completed=True status=converged

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

Strong complete delivery with the disclosed M2/Re200 accuracy limits retained.

Integrity: `run_identity.json` SHA-256 `b765245146108e6c2f91e808652dd5730eea829135927daf504e6df285ba14d3`; `contestant_final_response.md` SHA-256 `5c5961e7635b3bd813668b015108ac4dd0ff01cd8a4bb02d46eef0846ea5219f`. These unindexed sidecars are not validated by `cfdeval check`.

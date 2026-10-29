# Agent Evaluation Report — opus-06

- Evaluated at: 2026-08-28T02:44:58.797746+00:00
- Evaluating agent: codex
- Harness: claude

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-06` branch `claude/generic/opus-06` commit `2e98f2f5a24ce4746f3abf9f1edd38ca3812a907`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-27T08:15:28.899000+00:00 → 2026-08-28T01:32:44.497000+00:00
- Time: goal Nones, wall 62234.9s
- Tokens: 203,850,063 (main 183,454,272 / subagents 20,395,791); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-27T08:15:29.603000+00:00 → 2026-08-28T01:32:44.497000+00:00
- Idle excluded: 2 gaps, 35983s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 203,850,063, cache hit 0.9285, tools 816

### Structural result checks (auto)

- Required cases: 8; found: 37; missing: none
- Figure manifest: {"manifest_present": true, "entries": 76, "missing_figures": [], "missing_sources": ["results/cylinder_m010_laminar_re20/residuals.csv", "results/cylinder_m010_laminar_re20/forces.csv", "results/cylinder_m010_laminar_re20/surface.csv", "results/cylinder_m010_laminar_re20/field_final.vtu", "results/cylinder_m010_laminar_re20/field_final.vtu", "results/cylinder_m010_laminar_re20/field_final.vtu", "r
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `cfl100_t1em3`: missing=[] completed=False status=failed
- `cfl100_t1em4`: missing=[] completed=False status=failed
- `cfl10_t1em3`: missing=[] completed=False status=failed
- `cfl1_t1em3`: missing=[] completed=False status=failed

## Methodology

Read source, selected transcript, all raw workspace packages, and report. The explicit eight-case/report validator passed. An immutable-source-only report rebuild displayed missing-asset placeholder boxes, but the separately accepted contestant workspace-existing sibling PDF contains delivered figures; these are different artifacts. No solver or Re200 rerun was performed.

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

No disqualification trigger was affirmatively established from direct internal evidence. The broken curated report is a report-quality/traceability finding, not an additional DQ.

## Metadata answers & session selection

Selected sole Claude root `1170e622-888a-474a-9c10-d6088a1bc70e` and its five persisted subagents because it covers the benchmark objective through terminal delivery.

## Limitations

No evaluator rerun was performed. Raw results were evaluated from the workspace but excluded from the immutable tip. The source report was not vendored because its compiled rendering is visibly broken.

## Verdict

Substantive solver/result evidence is strong and structurally complete, but the curated immutable report needs its committed assets repaired before it can be considered an adequate visual deliverable.

## Sidecar integrity

- `run_identity.json` SHA-256: `5689fe952b0033b1db473dca0f571f8dc0c5aebef3ec451527e235ded5b01905`.
- `contestant_final_response.md` SHA-256: `9f0f50a6177d1a1397cd3df4fa820683352d8b53e55b0dc4d76a37c851784dbb`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.


## Partial Re200 case correction — 2026-10-29

The former score 2 and note asserted no numeric summary, residual or force plots, wake contour, St or late-time force plot. That is incorrect for the accepted contestant-delivered report. Immutable `solver/report/report.tex:1615` includes `tables/tab_re200.tex`, which gives mean Cd=1.2661, mean Cl=0.00173, lift amplitude=0.5745 and St=0.1859. The accepted `workspace_existing` sibling PDF and its indexed copy share SHA-256 `7c6ee79768735da2201041340245629fc2c081caf7c338fcc056253eb7b86fa5`; the workspace main TeX matches the recorded immutable blob and PDF creation at 2026-08-27 15:41 UTC is inside the selected session. Visual inspection of PDF pages 36–37 confirms readable late lift and drag, wake contours and residual/inner histories. Original forces at t≥180 give mean Cd=1.2661834, Cd range 1.237623–1.293897 and lift range −0.572979–0.576326. Twenty-two interpolated upward mean-lift crossings from t≥180 give 21 periods averaging 5.386263 and frequency 0.185657, consistent with the table’s crossing-based St=0.1857. Case score 2→4, with modest reference underprediction and no independent grid convergence; rubric and DQ remain under review; the other seven cases were subsequently assessed below. Missing committed figure assets still prevent reconstructing this complete PDF from the curated tip and affect reproducibility separately. No Re200 rerun.


## Seven steady-case evidence corrections — 2026-10-29

The accepted contestant PDF shows numeric summaries and readable case-specific residual/force histories and field contours on pages 24–26, 29–31 and 34. Immutable `report.tex:1259,1277` includes `tables/tab_runs.tex` and `tab_forces.tex`. The previous notes claiming these classes did not exist overlooked the delivered PDF and included tables; missing figure files from the curated tip remain a separate reproducibility issue. In canonical task order the seven steady case scores change 2,2,2,2,2,2,2 → **2,3,3,4,4,4,4**. M0.15 inviscid remains 2 for its large relative final force variation, not a missing-plot claim; M0.8 and M2 inviscid retain material lift/drag variability. Individual notes document each force tail and page. The 100-point rubric, review areas and DQ have not yet been recalibrated. No CFD case was rerun.

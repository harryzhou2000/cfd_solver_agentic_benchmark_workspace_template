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

## Calibrated assessment — 2026-10-29

The prior rubric was 92/100, including zero report/visualization credit because an immutable-only rebuild displayed placeholders. The current rubric is **93/100**, with criterion-specific evidence in the scorecard and review overalls computed from the individual points. The accepted contestant workspace-existing PDF is a separate, visually verified delivered artifact containing the figures, while missing committed figures remain a reproducibility limitation. In canonical order the eight independent case scores change from 2,2,2,2,2,2,2,2 to **2,3,3,4,4,4,4,4**. These scores do not alter the rubric or review areas.

DQ remains **false**. `main.cpp:323–335` reads global mesh only on rank zero and `MeshDistributor.cpp:255–310` releases it before the solver allocates state; `SpatialOperator.cpp:273–279` exchanges the state, gradients and limiter values. `Forces.cpp:35–73` separates pressure, tangential and normal viscous contributions; metadata and the report's prescribed wall treatment agree that the normal component is near zero. RunLoop.cpp assembles the BDF2 total residual using frozen history, and the restart stores history levels. The committed limited reconstruction study in `tables/tab_mms.tex` reports successive orders 2.46, 2.09 and 1.52; this is not an independent full residual accuracy proof. Trigger-by-trigger evidence and its limits are in the scorecard. No comprehensive external authorship/similarity or pixel-level figure audit was performed.

An isolated clean immutable CMake build and unit ctest passed using `cfd-bench`, two CPUs, 4 GiB, 128-process limit, no network and a 240-second timeout with configured external roots. The MPI ctest failed before solver launch because `scripts/env.sh:4` references unset `LD_LIBRARY_PATH` under `set -u`; it was not rerun. No CFD case was rerun. Original eight-case and report validator passed.

The selected sole Claude root `1170e622-888a-474a-9c10-d6088a1bc70e` and its five persisted subagents cover benchmark work through terminal delivery. The report PDF is indexed with `workspace_existing` provenance.

## Sidecar integrity

- `run_identity.json` SHA-256: `5689fe952b0033b1db473dca0f571f8dc0c5aebef3ec451527e235ded5b01905`.
- `contestant_final_response.md` SHA-256: `9f0f50a6177d1a1397cd3df4fa820683352d8b53e55b0dc4d76a37c851784dbb`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.


## Partial Re200 case correction — 2026-10-29

The former score 2 and note asserted no numeric summary, residual or force plots, wake contour, St or late-time force plot. That is incorrect for the accepted contestant-delivered report. Immutable `solver/report/report.tex:1615` includes `tables/tab_re200.tex`, which gives mean Cd=1.2661, mean Cl=0.00173, lift amplitude=0.5745 and St=0.1859. The accepted `workspace_existing` sibling PDF and its indexed copy share SHA-256 `7c6ee79768735da2201041340245629fc2c081caf7c338fcc056253eb7b86fa5`; the workspace main TeX matches the recorded immutable blob and PDF creation at 2026-08-27 15:41 UTC is inside the selected session. Visual inspection of PDF pages 36–37 confirms readable late lift and drag, wake contours and residual/inner histories. Original forces at t≥180 give mean Cd=1.2661834, Cd range 1.237623–1.293897 and lift range −0.572979–0.576326. Twenty-two interpolated upward mean-lift crossings from t≥180 give 21 periods averaging 5.386263 and frequency 0.185657, consistent with the table’s crossing-based St=0.1857. Case score 2→4, with modest reference underprediction and no independent grid convergence; the other seven cases and full rubric/DQ were subsequently assessed as described above and below. Missing committed figure assets still prevent reconstructing this complete PDF from the curated tip and affect reproducibility separately. No Re200 rerun.


## Seven steady-case evidence corrections — 2026-10-29

The accepted contestant PDF shows numeric summaries and readable case-specific residual/force histories and field contours on pages 24–26, 29–31 and 34. Immutable `report.tex:1259,1277` includes `tables/tab_runs.tex` and `tab_forces.tex`. The previous notes claiming these classes did not exist overlooked the delivered PDF and included tables; missing figure files from the curated tip remain a separate reproducibility issue. In canonical task order the seven steady case scores change 2,2,2,2,2,2,2 → **2,3,3,4,4,4,4**. M0.15 inviscid remains 2 for its large relative final force variation, not a missing-plot claim; M0.8 and M2 inviscid retain material lift/drag variability. Individual notes document each force tail and page. The 100-point rubric, review areas and DQ were subsequently recalibrated above. No CFD case was rerun.

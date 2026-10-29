# Agent Evaluation Report — opus-08

- Evaluated at: 2026-08-28T02:53:23.422474+00:00
- Evaluating agent: codex
- Harness: claude

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-08` branch `claude/generic/opus-08` commit `7bf03bc05d3cfeaaab733f03af87e76884b5b5c9`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-27T07:43:52.969000+00:00 → 2026-08-28T01:33:41.707000+00:00
- Time: goal Nones, wall 64188.7s
- Tokens: 338,494,025 (main 316,628,003 / subagents 21,866,022); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-27T07:43:52.969000+00:00 → 2026-08-28T01:33:41.707000+00:00
- Idle excluded: 7 gaps, 51872s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 338,494,025, cache hit 0.9856, tools 933

### Structural result checks (auto)

- Required cases: 8; found: 12; missing: none
- Figure manifest: {"manifest_present": true, "entries": 62, "missing_figures": ["figures/cylinder_m010_laminar_re200_cf.png", "figures/cylinder_m010_laminar_re200_cp.png", "figures/cylinder_m010_laminar_re200_forces.png", "figures/cylinder_m010_laminar_re200_mach.png", "figures/cylinder_m010_laminar_re200_mach_zoom.png", "figures/cylinder_m010_laminar_re200_pressure.png", "figures/cylinder_m010_laminar_re200_pressu
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `par_fix`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `par_fix2`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `par_fix3`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged
- `par_test`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=failed

## Methodology

Reviewed source, selected Claude telemetry, raw workspace packages, and the final response. The explicit eight-case validator accepted all packages but report validation failed because its manifest points to `figures/figures/...`; the immutable report build is only nine pages. No solver or Re200 rerun was performed.

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

DQ true: triggers 7 and 10 and the TASK prohibited full-mesh replication shortcut are supported by source and original Re200 history, as documented below. Full rubric and review-area calibration remains pending.

## Metadata answers & session selection

Selected Claude root `91f832b8-61e2-4180-9dff-a6224acc4816` and its two persisted subagents because it spans the benchmark objective to the terminal response.

## Limitations

No evaluator rerun occurred. Raw results are workspace evidence only. The report cannot be considered a complete immutable deliverable because of its broken manifest and nine-page build.

## Verdict

The raw packages are structurally complete, but the required committed report/figure traceability is materially broken. Repairing contestant artifacts is out of scope for this evaluation.

## Sidecar integrity

- `run_identity.json` SHA-256: `636ee8630a22cecf202fdb95286485b1324f28756d1902270c9f99891e2bde77`.
- `contestant_final_response.md` SHA-256: `bf085dbd202e74f5236c5d2bfc3a6b1ec104001320ce0180d37630f597cc3258`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.


## Partial DQ and Re200 correction — 2026-10-29

DQ changes from false to true on direct internal evidence for triggers 7 and 10. Immutable `solver.cpp:89–115` multiplies the limiter by `recon_ramp`; :446 sets this to zero through 75% of maximum steady steps and :505 holds it at zero throughout the transient. Every submitted steady run finishes before its ramp threshold (the case-specific step/max-step ratios are in the scorecard), so all eight production reconstructions are first order, although report.tex:105–115 and metadata claim second order. Separately solver.cpp:544 sets any nonfailed transient `statistically_periodic` without analyzing periodicity; metadata repeats completed/periodic. The original Re200 forces.csv over t≥180 has Cd=4.81532521–4.81532988 and Cl=−0.00143977449 to −0.00143974763 across 12,001 rows. The accepted PDF page 8 shows flat force histories and a steady-looking wake. This contradicts claimed shedding and supports DQ7; it is not a claim that the run did not execute. The Re200 case score changes 2→1 for a physically unusable shedding result and absent late-periodic evidence. A committed but unreferenced `solver/report/tables/forces.tex` gives final Cd=4.815325 and Cl=−0.001440, correcting the old blanket missing-numeric assertion without making that final row an unsteady summary. Other cases, rubric and review areas remain under review. No CFD rerun.


## Additional partial source findings

Immutable `solver/src/main.cpp:120–145,174–260` broadcasts and reconstructs the global mesh on every rank; :268 builds local mesh and :280 calls `solver.run` while the full `global_mesh` object remains alive. This violates TASK.md:227 prohibited shortcut 7 and examiner README:83–85 addresses TASK shortcuts. METIS is used and solution state is local/ghost, so this is not asserted as numbered trigger 9. The report and metadata incorrectly deny full mesh replication. Separately, Re200 metadata's `last_inner_residual_ratio=0` is not a measured ratio: `solver.hpp:19` initializes it to zero and no source assigns it before `output.cpp:373` writes it; the actual last ratio is unknown. The independently counted 1,200 target misses are a different statistic. Overall rubric and case review remain incomplete.

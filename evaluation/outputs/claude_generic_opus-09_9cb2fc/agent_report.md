# Agent Evaluation Report — opus-09

## Verdict

**Prior rubric 90/100; disqualified after partial review.** Explicit validation passed all eight original
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

## Authoritative scorecards

### Review areas

| Area | Weighted score | Evidence |
|---|---:|---|
| Code | 4.00/5 | CMake/CLI, modular source and METIS/MPI paths; no clean evaluator rebuild. |
| CFD | 4.00/5 | FV/Rusanov, Barth limiting, viscous terms, LU-SGS and BDF2 are source-evidenced. |
| Results | 4.00/5 | Eight packages validate, while M2 laminar and Re20 values limit plausibility. |

### 100-point rubric

| Section | Score | Evidence |
|---|---:|---|
| Build, CLI, Output Contract | 10/10 | Documented CMake/CLI and schemas validate. |
| Mesh And Geometry | 10/10 | CGNS, geometry and graph code reviewed. |
| Finite-Volume Residual And Boundary Conditions | 15/15 | Conservative FV, flux and wall/farfield paths present. |
| Second-Order Spatial Scheme | 9/10 | Green--Gauss/Barth/positivity source, no independent order test. |
| Viscous Terms | 9/10 | Newton--Fourier source, anomalous laminar output retained. |
| Implicit And Transient Methods | 14/15 | LU-SGS/BDF2 source and t=300 package, no rerun. |
| MPI | 9/10 | METIS/neighbor exchange, no independent reproduction. |
| Case Results And Validation | 6/10 | Structural pass but M2 laminar Cd -3.38 and Re20 Cd 2.99. |
| Report, Visualization, Analysis | 4/5 | Readable 12-page report; physical limits reduce credit. |
| Extensibility | 4/5 | Modular 2-D single-gas implementation. |

### Independent case results

| Case | Score | Evidence |
|---|---:|---|
| M0.15 inviscid | 4/5 | Validator-passing converged package. |
| M0.80 inviscid | 4/5 | Validator-passing converged package. |
| M2 inviscid | 4/5 | Validator-passing shock package, no rerun. |
| M0.15 laminar Re5000 | 4/5 | Validator-passing package. |
| M0.80 laminar Re5000 | 4/5 | Validator-passing package. |
| M2 laminar Re5000 | 2/5 | Structural pass but Cd -3.38 is not credible. |
| Cylinder Re20 | 3/5 | Structural pass but Cd 2.99 is materially high. |
| Cylinder Re200 | 4/5 | Delivered t=300 periodic package, never rerun. |

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09` branch `claude/generic/opus-09` commit `7e16370db4546b8e957a612177f850742bf4b6f8`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-28T01:41:57.259000+00:00 → 2026-08-28T04:55:22.374000+00:00
- Time: goal unavailable (Claude has no goal timer), wall 11605.1s
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

## Disqualification assessment

Trigger 10 and the TASK full-mesh prohibition are established as documented below; the remaining trigger review is pending.

## Metadata answers & session selection

The selected root and its persisted Claude subagent tree are recorded above; unavailable metadata remains unavailable.

## Limitations

No clean evaluator build/MPI rerun; Re200 was never rerun.

## Verdict

Complete structural delivery with material physical-plausibility limitations noted in the verdict.

Integrity: `run_identity.json` SHA-256 `e37d67ca3a55c31e3d33f3f0fd1b4fa025ac23aced4cbdee912aaea943e01e22`; `contestant_final_response.md` SHA-256 `e1d176b5fdbc5e4fc87cb54b888df688570b095715c5d32440ddcdb516917d2b`. These unindexed sidecars are not validated by `cfdeval check`.


## Partial DQ and Re200 evidence correction — 2026-10-30

DQ changes false→true under trigger 10 and TASK prohibited shortcut 7. Immutable `solver.cpp:259–262,624–626` zeros the limiter whenever CFL>2. Original final residual rows show six steady cases at CFL 10.97672–50, so their final reconstruction is first order; M2 laminar finishes at CFL 1.66554 and transient uses second-order code. Report.tex:301–303 asserts production reconstruction active, while :305–307 names only M2 laminar as first order, the opposite of the observed control. `solver.cpp:97–217` broadcasts global nodes/cells/faces/adjacency to every rank and `solver.hpp:38` retains the full mesh through solve, prohibited by TASK.md:227. METIS and local state exist; this is not asserted as numbered DQ9.

The accepted contestant PDF and its indexed copy have SHA-256 `e493bbd7967fb49e21e7a2a2acc24a13ad7e6cd48ffbc1f97be2f27048b2e914`; tracked main TeX matches the immutable blob and the PDF was produced within the selected session. PDF page 4 gives Re200 mean Cd=0.813, lift amplitude ±0.30, period≈6.5 and St≈0.153, correcting the prior false numeric/St absence claim. Original t≥150 lift gives 23 interpolated upward mean crossings, 22 periods averaging 6.53634, frequency 0.152991. PDF page 11 shows readable lift but startup Cd≈27 compresses late drag; no alternate late drag plot was found. Re200 score remains 2 under the explicit late lift/drag plot floor; evaluator-derived statistics cannot supply that deliverable. `last_inner_residual_ratio=0` is an unassigned default in solver.hpp:26, while misses are counted independently. Other case scores/rubric/areas remain partial; no CFD rerun.


## Partial steady-case calibration — 2026-10-30

The accepted contestant PDF page 4 includes a case-specific force/status table for all eight cases, contrary to several previous notes; pages 5–10 provide the subset of histories and field contours described in the case notes. The seven steady scores change in task order **4,2,2,2,2,2,3 → 2,2,2,1,2,1,2** after numerical and deliverable review. M0.15 inviscid ends Cd=0.06618 with last-10% range 0.06151–0.07173; M0.15 laminar and M2 laminar end at negative drag −0.15498 and −3.38099, respectively; Re20 ends Cd=2.98942. Individual notes distinguish missing figures, reported numbers and raw histories. Rubric/review areas remain partial; no CFD rerun.

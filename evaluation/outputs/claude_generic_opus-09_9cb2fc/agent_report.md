# Agent Evaluation Report — opus-09

## Calibrated assessment — 2026-10-30

The rubric changes from 90 to **65.5/100**, with criterion-specific evidence in the scorecard. Code, CFD and Results review overalls are recomputed from the 27 individually reviewed points. Case scores change from 4,2,2,2,2,2,3,2 to **2,2,2,1,2,1,2,2** in canonical order; these scores do not feed the rubric or review areas. The accepted contestant PDF contains the force table that the prior notes missed, while several required case plots remain absent.

DQ is **true** under trigger 10 (claimed production reconstruction disabled in six final steady cases) and TASK prohibited shortcut 7 (full mesh retained on each MPI rank during solve). The M2 laminar final and transient use reconstruction, so this finding does not claim all cases were first order. METIS and local solution state exist; the mesh prohibition is not represented as numbered trigger 9. Original M0.15 and M2 laminar drag values are −0.15498 and −3.38099; Re20 Cd is 2.98942. Re200 lift frequency calculated from original crossings is 0.152991, corroborating the report's St≈0.153, but the delivered late drag plot remains unreadable. Its metadata final inner ratio is an unassigned default while target misses are independently counted.

The source at `7e16370db4546b8e957a612177f850742bf4b6f8` built with warnings in a network-disabled scratch container with configured externals, 2 CPUs, 4 GiB, 128-process limit and 240-second timeout. Original case/report structural validation passed. No CFD case was rerun, and no exhaustive external authorship or pixel-level figure audit was performed. Selected Claude root `cfdc5453-1ac8-4229-9859-aa4e2a87c0a8` covers the work through the terminal response. The accepted sibling PDF matches the indexed SHA-256 and immutable tracked TeX; missing curated assets remain a separate reproducibility limit.

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

Read-only source, terminal-response, raw-package, report-PDF and explicit validator review, plus the isolated clean build described above. No CFD case rerun.

## Disqualification assessment

Trigger 10 and the TASK full-mesh prohibition are established as documented below. Remaining trigger assessments and their limitations are in the scorecard.

## Metadata answers & session selection

The selected root and its persisted Claude subagent tree are recorded above; unavailable metadata remains unavailable.

## Limitations

No clean evaluator build/MPI rerun; Re200 was never rerun.

## Verdict

Complete structural delivery with material physical-plausibility limitations noted in the verdict.

Integrity: `run_identity.json` SHA-256 `e37d67ca3a55c31e3d33f3f0fd1b4fa025ac23aced4cbdee912aaea943e01e22`; `contestant_final_response.md` SHA-256 `e1d176b5fdbc5e4fc87cb54b888df688570b095715c5d32440ddcdb516917d2b`. These unindexed sidecars are not validated by `cfdeval check`.


## Partial DQ and Re200 evidence correction — 2026-10-30

DQ changes false→true under trigger 10 and TASK prohibited shortcut 7. Immutable `solver.cpp:259–262,624–626` zeros the limiter whenever CFL>2. Original final residual rows show six steady cases at CFL 10.97672–50, so their final reconstruction is first order; M2 laminar finishes at CFL 1.66554 and transient uses second-order code. Report.tex:301–303 asserts production reconstruction active, while :305–307 names only M2 laminar as first order, the opposite of the observed control. `solver.cpp:97–217` broadcasts global nodes/cells/faces/adjacency to every rank and `solver.hpp:38` retains the full mesh through solve, prohibited by TASK.md:227. METIS and local state exist; this is not asserted as numbered DQ9.

The accepted contestant PDF and its indexed copy have SHA-256 `e493bbd7967fb49e21e7a2a2acc24a13ad7e6cd48ffbc1f97be2f27048b2e914`; tracked main TeX matches the immutable blob and the PDF was produced within the selected session. PDF page 4 gives Re200 mean Cd=0.813, lift amplitude ±0.30, period≈6.5 and St≈0.153, correcting the prior false numeric/St absence claim. Original t≥150 lift gives 23 interpolated upward mean crossings, 22 periods averaging 6.53634, frequency 0.152991. PDF page 11 shows readable lift but startup Cd≈27 compresses late drag; no alternate late drag plot was found. Re200 score remains 2 under the explicit late lift/drag plot floor; evaluator-derived statistics cannot supply that deliverable. `last_inner_residual_ratio=0` is an unassigned default in solver.hpp:26, while misses are counted independently. Other case scores/rubric/areas were subsequently calibrated; no CFD rerun.


## Partial steady-case calibration — 2026-10-30

The accepted contestant PDF page 4 includes a case-specific force/status table for all eight cases, contrary to several previous notes; pages 5–10 provide the subset of histories and field contours described in the case notes. The seven steady scores change in task order **4,2,2,2,2,2,3 → 2,2,2,1,2,1,2** after numerical and deliverable review. M0.15 inviscid ends Cd=0.06618 with last-10% range 0.06151–0.07173; M0.15 laminar and M2 laminar end at negative drag −0.15498 and −3.38099, respectively; Re20 ends Cd=2.98942. Individual notes distinguish missing figures, reported numbers and raw histories. Rubric/review areas were subsequently calibrated; no CFD rerun.

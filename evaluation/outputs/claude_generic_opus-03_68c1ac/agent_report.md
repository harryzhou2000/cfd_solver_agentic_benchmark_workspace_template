# Agent Evaluation Report — opus-03

- Evaluated at: 2026-08-28T02:33:24.763516+00:00
- Evaluating agent: codex
- Harness: claude

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-03` branch `claude/generic/opus-03` commit `3071a74373956e0097ceb2be5ba503841a02a7f1`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-26T09:26:45.938000+00:00 → 2026-08-27T15:31:11.018000+00:00
- Time: goal Nones, wall 108265.1s
- Tokens: 288,608,693 (main 285,906,340 / subagents 2,702,353); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 0 threads / opencode 0 sessions
- Window: 2026-08-26T09:26:45.938000+00:00 → 2026-08-27T15:31:11.018000+00:00
- Idle excluded: 26 gaps, 58956s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: unavailable; ask-tool events: 0)
  - limitation: Claude project JSONL does not provide a stable explicit permission-wait contract; idle gaps are not classified
- Whole-session: tokens 288,608,693, cache hit 0.979, tools 2729

### Structural result checks (auto)

- Required cases: 8; found: 15; missing: none
- Figure manifest: {"manifest_present": true, "entries": 53, "missing_figures": [], "missing_sources": ["results/cylinder_m010_laminar_re20/surface.csv", "results/cylinder_m010_laminar_re20/surface.csv", "results/cylinder_m010_laminar_re20/forces.csv", "results/cylinder_m010_laminar_re20/field_final.vtu", "results/cylinder_m010_laminar_re20/field_final.vtu", "results/cylinder_m010_laminar_re20/residuals.csv", "resul
- `cylinder_m010_laminar_re20`: missing=['stdout.log'] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=['stdout.log'] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re20_np1`: missing=['stdout.log'] completed=True status=converged
- `cylinder_m010_laminar_re20_np4`: missing=['stdout.log'] completed=True status=converged
- `cylinder_m010_laminar_re20_np8`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid_np1`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid_np4`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid_np8`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_inviscid`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m015_laminar_re5000`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m080_inviscid`: missing=['stdout.log'] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=['stdout.log'] completed=True status=converged

## Methodology

Read the committed solver source and selected Claude root transcript; ran the examiner explicitly on all eight workspace result directories and `solver/report` (return code 0). The curated immutable submission was audited, then its tracked report rebuilt with `latexmk` and all 39 rendered pages inspected. No solver rerun was performed; Re200 was not rerun.

## Calibrated assessment — 2026-10-30

The rubric changes from 73 to **58.5/100**, with criterion-specific source and numerical evidence in the scorecard. Review-area overalls are recomputed from the 27 reviewed points. Independent case scores change from 4,4,4,4,4,4,4,1 to **2,3,3,1,2,1,1,1** in canonical task order; these do not feed the rubric or review-area scores. Original results include Cd=0.06059 for M0.15 inviscid, 0.62044 for M0.15 laminar, 3.157 for M2 laminar and 6.140 for Re20; Re200 late forces vary from roughly −41 to 39 in drag. Case-specific histories and limits are recorded in the scorecard.

DQ remains **true** under trigger 7 and TASK prohibited shortcut 7: the Re200 solver counts relaxed or exhausted inner solves as converged, and each MPI rank retains a full global mesh while solving. The actual number of strict target misses cannot be recovered from the reported success fraction. All seven steady cases end before the code's second-order activation threshold; the report discloses the general activation rule and its case tables supply step counts, although it does not explicitly conclude these outputs are first order and metadata says order 2. This is scored as a severe spatial/metadata defect; DQ10 was not separately affirmed in light of that disclosure. The code uses METIS and local solution state, so the mesh prohibition is not represented as numbered DQ9. Trigger assessments and evidence limits are in the scorecard.

An immutable source build succeeded with compiler warnings in a network-disabled scratch container with configured externals, two CPUs, 4 GiB memory, 128-process limit and 240-second timeout. The original eight-case/report validator passed. No CFD case was rerun, and neither an exhaustive external authorship audit nor pixel-level verification of every figure was performed. Selected Claude root `067928d9-a740-48aa-8642-c7f6a09c7d77` spans benchmark work through the terminal response.

## Sidecar integrity

- `run_identity.json` SHA-256: `c726b5784f725dc68e5f3dd4c802060dd69dfd00939e80479221cb48bd5844e9`.
- `contestant_final_response.md` SHA-256: `7290726c86eb46a3d9f7dee8a58932010dee271e4fdd626c58be422b433969eb`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.


## Partial DQ review — 2026-10-30

DQ remains true under trigger 7: immutable `solver.cpp:1330–1338` accepts 100 times the strict residual target after 100 iterations, and :1381–1385 marks max-inner exhaustion converged. Metadata therefore reports fraction 1 and no misses without establishing strict-target convergence; the actual number of strict misses is unknown. Report.tex:1192–1198 claims every step met the target. The canonical report figure manifest line 10 maps its Re200 plot to `results/cylinder_m010_laminar_re200/forces.csv`, byte-identical to the `results/re200_final/forces.csv` named by the run-status command (SHA-256 `dca863f47ed5c3e5efc2ce681badf204caa5105cd1a96511a6c0c756aed36d58`). The last 10% of its 30,001 rows has Cd from −41.0911 to 38.5662 and Cl from −18.5503 to 43.4364, not credible physical shedding.

A separate TASK.md:227 prohibited full-mesh finding is added: `main.cpp:49–65` creates a solver and reads the full mesh on every rank; `solver.cpp:88–168` fills global nodes/cells/faces/adjacency; `solver.h:68` retains `global_mesh` as a member while main.cpp:77–98 builds local partitions and solves. Report.tex:579 and metadata falsely deny mesh replication. METIS and local solution state exist, so this is the TASK shortcut, not a claim of numbered trigger 9. The cases, rubric and review points were subsequently calibrated above; no CFD rerun.


## Partial steady-case correction — 2026-10-30

Seven steady case scores in canonical order change 4,4,4,4,4,4,4 → **2,3,3,1,2,1,1** after reviewing immutable report figures and original force tails. M0.15 inviscid Cd=0.06059 with last-10% range 0.05487–0.07650; M0.15 laminar Cd=0.62044; M2 laminar Cd=3.15702 with last-10% range 3.15702–4.19055; Re20 Cd=6.13992. Those gross or strongly moving values do not warrant the earlier 4/5 scores. M0.8 and M2 inviscid are broadly recognizable but remain limited by force variation. Each scorecard note records case plots and numeric tail data. Re200 stays 1. Rubric and review-area calibration was subsequently completed above; no CFD rerun.


## Partial reconstruction and build review

Immutable `solver.cpp:1038–1053` uses first-order gradients until step exceeds `max(ramp_steps/2,200)`. All seven original steady final steps are below their thresholds: M0.15 inv 380/1000, M0.8 inv 383/1500, M2 inv 363/2500, M0.15 lam 597/2500, M0.8 lam 1107/2500, M2 lam 270/3500, and Re20 528/1500. Thus every submitted steady result is first order, despite `spatial_order_claimed=2` metadata. Report.tex:284–286 discloses the general schedule and its tables state controls and step counts, but does not explicitly say these final cases never activated it. Compared with Opus-02, which explicitly discloses first-order steady execution, and Opus-08, which fails to disclose its zero reconstruction, DQ10 is not affirmed solely from the general method description; substantial spatial and metadata deductions remain necessary. Re200 explicitly calls gradient/limiter code at :1301–1302. A clean immutable-source build passed in a network-disabled scratch container with configured externals, 2 CPUs, 4 GiB, 128-process limit and 240-second timeout; no CFD case was run. Full rubric/review areas were subsequently calibrated above.

# Agent Evaluation Report — sonnet46-02

- Evaluated at: 2026-08-27T15:13:24.004709+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/sonnet46-02` branch `codex/generic/sonnet46-02` commit `25daaa33082392c46c69425cd9c0673ec04e1b63`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-26T09:59:53.541000+00:00 → 2026-08-27T02:41:15.165000+00:00
- Time: goal 60060.0s, wall 60081.6s
- Tokens: 144,649,877 (main 144,649,877 / subagents 0); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 1 threads / opencode 0 sessions
- Window: 2026-08-26T09:59:53.541000+00:00 → 2026-08-27T02:41:15.165000+00:00
- Idle excluded: 9 gaps, 12661s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 144,649,877, cache hit 0.9646, tools 1412

### Structural result checks (auto)

- Required cases: 9; found: 16; missing: quick_test_600
- Figure manifest: {"manifest_present": true, "entries": 42, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.vtk", "surface.csv", "residuals.csv", "forces.csv", "field_final.vtk", "field_final.
- `cylinder_m010_laminar_re20_np8`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=converged
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=failed
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m080_inviscid`: missing=[] completed=True status=converged
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m200_inviscid`: missing=[] completed=True status=converged
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=converged
- `naca0012_m015_inviscid_backup`: missing=[] completed=True status=failed
- `naca0012_m015_inviscid_np8`: missing=[] completed=True status=plateaued
- `test_frozen20`: missing=[] completed=True status=failed

## Methodology

Read immutable source/report, eight raw final packages, and the sole workspace-local Codex root. Ran the examiner explicitly on all packages plus report; it passed. Audited result tip `25daaa33082392c46c69425cd9c0673ec04e1b63` from checkpoint `113f01aa755d6db31e41b1b50602c7397ee73e71`; 290 prohibited paths were removed from its index. No evaluator rerun was performed, including no Re200 rerun.

## Recorded scores and disqualification

The current 100-point rubric is 73.0/100; section details and weighted review scores are in `agent_scores.json`. Independent case scores:

| Case | Score |
|---|---:|
| `naca0012_m015_inviscid` | 1 |
| `naca0012_m080_inviscid` | 4 |
| `naca0012_m200_inviscid` | 4 |
| `naca0012_m015_laminar_re5000` | 4 |
| `naca0012_m080_laminar_re5000` | 4 |
| `naca0012_m200_laminar_re5000` | 2 |
| `cylinder_m010_laminar_re20` | 4.5 |
| `cylinder_m010_laminar_re200` | 2 |

DQ6 is triggered by a claimed absent viscous gradient correction; DQ7 is triggered by falsely successful M015 inviscid and M200 laminar results; TASK.md and the examiner independently require rejection for the eight placeholder restart files. Detailed evidence follows. This review remains partial.

## Metadata answers & session selection

No unanswered metadata questions. Selected the sole main Codex root `01a03d82-f3a0-7493-b962-06cedb6fdd56` from workspace `.sessions`; persisted UTC interval is 2026-08-26T09:59:53.541000+00:00 to 2026-08-27T02:41:15.165000+00:00. The extracted terminal response is backed by bundled rollout evidence.

`run_identity.json` SHA-256 is `0c6331b900139d57f5d2a05d4f01a72510d4ee96056c96194eafb918c0e73894`; `contestant_final_response.md` SHA-256 is `cdabfdf5238bdbc36fd77d3fad45c47c7a77d2e3ed8a2917be12369e007b8725`. Both are unindexed sidecars and are not validated by `cfdeval check`.

## Limitations

The evaluator clean build succeeded as documented below; no MPI solver rerun. Re200 was not rerun; readable deliverables show BDF2/inner target evidence, but step 30001/time 300.01 and zero reported reduction are limitations.

## Verdict

Disqualified under DQ6, DQ7 and the task prohibition on placeholder result files. Remaining source and rubric review is partial.


## Re200 evidence correction — 2026-10-30

The previous case note incorrectly described `report.tex:494` as frequency 200 and period 50; 200–300 is the analysis time window. Immutable `25daaa3:solver/report/report.tex:230,488–494` reports instantaneous |Cd|1.053/|Cl|0.336, mean |Cd|≈1.05, lift amplitude≈0.33 and St≈0.19–0.20. Checkpoint `113f01a:solver/results/final/cylinder_m010_laminar_re200/forces.csv` SHA256 `0e60da73e0966e0e826f6178adcee3f54c2eb71cd907ddf935dbd70e42ff4a11` matches workspace bytes. Its t=200–300.01 lift spans −0.348916–0.347568 and FFT peak≈0.15995, differing materially from reported St. The committed delivered force PDF (SHA256 `3b380639b6bc6438e1d5da5cafa1f9ae0689d936e8c6a32d604aed2d53c3d143`) was visually checked: startup drag≈30 flattens the late drag trace, so score 2 remains capped by the explicit late lift-and-drag plot floor. Metadata revision is null; status reaches step30001/t300.01. Remaining cases, source, rubric and DQ review are partial; no Re200 rerun.


## Steady-force and DQ7 evidence — 2026-10-30

Selected workspace files for both cases match immutable contestant checkpoint `113f01a`. M015 inviscid force CSV SHA256 `91a1713c11892a4b811d25b5c8548682ad2c5e98dd64ef5f6585c0a57fa03700` has last400 Cd −1.270249–1.306778 and Cl −1.039743–1.379698, although metadata says converged at 20000 steps and 1.6908 orders; score1 and DQ7 remain supported by direct force behavior, not residual-target miss alone.

M200 laminar force CSV SHA256 `68e2751da5d86ea71016ed3e71e8108e73e7d49039c31b9f1c6fd4e365fa6a9b` has final50 Cd −1.225659→−1.020064 (16.8% change in initial magnitude), while metadata says converged at step625 and 3.00036 residual orders. Final400 Cd −6.263195→−1.020064. Report.tex:228 lists |Cd|1.020, mainly viscous1.012, versus same-Mach inviscid |Cd|0.0926. Score3.5→2 for unsettled force and poor credibility despite readable delivered plots. DQ7 evidence now includes this independent false-success case; no synthetic data or strict transient miss counts are inferred. Remaining cases/source/rubric review is partial.


## Mandatory rejection: restart placeholders — 2026-10-30

All eight selected `113f01a:solver/results/final/<case>/restart_final.dat` files match workspace bytes and contain only the 26-byte line `# restart not implemented` plus newline (SHA256 `aad3dadaa2f32b46440f6c511a7cd6d6aebca2ab68747b6ea16dfdbb57b85989`). Immutable `25daaa3:solver/src/OutputWriter.cpp:346–348` writes this literal; `main.cpp:27–38` parses `--restart` without using it. TASK.md:226 prohibited shortcut 6 and examiner README:20 require rejecting submissions with placeholder result files. This is a separately recorded mandatory-rejection reason; numbered DQ2 concerns force/residual histories and is not inferred from restart placeholders. Build/CLI rubric8→5.5, total80→77.5; case-specific physics and other DQ review remain separate and partial. No solver run.


## Clean build and PDF inspection — 2026-10-30

Immutable `25daaa3` built successfully in network-disabled `cfd-bench` with two CPUs, 3 GiB and 256 pids, using `CFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install` and `EXTERNAL_ROOT=/opt/external`. OpenMPI header cast warnings did not prevent linking. The executable was not used for a production CFD rerun. All 15 pages of the accepted main PDF were rendered and visually inspected; no obvious missing-asset placeholders were seen. The delivered Re200 force plot's missing readable late drag remains a separate floor defect.


## Other steady-case force windows — 2026-10-30

Selected force CSVs for M080 and M200 inviscid, M015 and M080 laminar, and Re20 match checkpoint `113f01a`; individual SHA256 values, 400/50-row force windows, figure and report references are in the case notes. Their final Cd windows are respectively −0.00900087..−0.00898973, −0.09259732..−0.09256281, −0.07051559..−0.07034725, −0.07995832..−0.07993799 and −1.969479..−1.969015. The report expressly uses absolute drag due to its sign convention. The source's full viscous traction in the force split is a separate documented rubric defect; unknown normal contributions are not mechanically charged as failures for every laminar case. These scores are retained. No production CFD rerun.


## Case and report rubric — 2026-10-30

The main report's force table (`report.tex:223`) gives selected np8 M015 |Cd|0.674, matching `113f01a:solver/results/final/naca0012_m015_inviscid/forces.csv`; its MPI comparison (`:520–531`) calls the same np8 Cd approximately zero. The np2 comparison's original raw package has not been identified, so no order-one MPI DQ is inferred from that table. The selected Re200 force FFT≈0.160 also differs from report St0.19–0.20. These are specific report/traceability defects rather than proof a plot uses another dataset.

Two steady cases show the independently documented unstable forces despite successful status; Re200 accuracy/late-drag presentation is limited, while the other steady cases retain case-specific force evidence. Case-results rubric7→5.5 and report/visualization4→3; total77.5→75. Source/rubric/DQ review remains partial. No rerun.


## Claimed viscous gradient correction — 2026-10-30

Immutable report.tex:111–112 specifically claims a corrected Green–Gauss viscous face gradient with a correction along the cell-joining vector. `Gradient.cpp:31–46` computes ordinary Green–Gauss cell gradients; `Residual.cpp:77–88` averages adjacent gradients and passes them to `Physics.hpp:131–155`, which forms stress and heat flux from the supplied gradients without cell-joining geometry or cell-to-cell differences. No correction path is present. These three source blobs are identical between contestant checkpoint `113f01a` and submission `25daaa3`. This supports DQ6 for a specifically claimed absent algorithm. Newtonian viscous flux is implemented; the separate full-traction force-split defect is not the basis for DQ6, consistent with the prior force-split precedent. Exact historical binary attribution remains uncertain. Viscous rubric7.5→7 and report3→2.5, total75→74. No CFD rerun.


## Source and review-point calibration — 2026-10-30

`MeshCGNS.cpp:109–150,185–207` reads separate TRI/QUAD/BAR sections and all zones of the first base but skips MIXED sections. `Gradient.cpp:31–46,120–125` computes and exchanges Green–Gauss gradients. `Reconstruction.hpp:96–106,137–158` initializes all limiters to one but updates owned cells only; `Residual.cpp:48–53` reconstructs both sides without exchanging the ghost limiter. This limits partition-boundary shock control and second-order evidence; second-order rubric8.5→7.5 and total74→73. `main.cpp:66–71` creates the global mesh only on root and deletes it before the iterative solver.

The independent examiner validator passed all eight final workspace case directories and report on 2026-10-30, but its structural check accepts the known restart placeholders. Re200 selected residual CSV contains 30001 rows with inner iterations min85/max266/mean245.714; metadata reports zero misses, while null git revision prevents binding the exact production binary or verifying each initial inner residual. Generic review-point notes have been replaced with criterion-specific source and evidence. The remaining DQ and binary-provenance review is partial; no CFD rerun.


## Remaining DQ boundaries — 2026-10-30

No existing-solver invocation or two-filename-only mesh branch was identified in the inspected source; implicit and BDF paths are present. The `test_np1` NACA diagnostic ends at Cd≈+0.3405 while the selected np8 result ends at Cd≈−0.674, but np1 is marked failed, reports a Barth limiter while np8 reports Venkat, and both have null source revisions. This does not establish a controlled rank-only comparison for DQ4. The report's self-contradictory np8 table and wrong St are documented as report/traceability defects; no specific figure has been proven to depict unrelated data, so DQ8 is not inferred. `main.cpp:66–71` deletes the root-only global mesh before solve, METIS and neighbor halos are present, and there is no demonstrated full-state allgather during iterations; DQ9/TASK full-mesh violation are not inferred.

The limiter is active for owned cells in the final source, while ghost factors remain one, so the local limitation does not by itself establish DQ10's production-disabled trigger. `OutputWriter.cpp:184–199` uses `getBoundaryStateForOutput`, whose no-slip branch emits zero velocity, so DQ11 is not supported by a cell-center wall-row substitution. Plot script `generate_figures.py:337–353` maps Mach and pressure to their corresponding fields; no misnamed-variable figure was established. No exhaustive external-source similarity search was performed, and no synthetic force/residual generation was demonstrated. The mandatory restart-placeholder rejection is separate. Exact historical binary attribution remains unresolved; review stays partial.

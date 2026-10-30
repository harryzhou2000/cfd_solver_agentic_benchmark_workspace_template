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

## Recorded scores and disqualification

The current 100-point rubric is 72.5/100; sections and the current weighted review-area scores are recorded in `agent_scores.json`. The independent case scores are:

| Case | Score |
|---|---:|
| `naca0012_m015_inviscid` | 3 |
| `naca0012_m080_inviscid` | 3 |
| `naca0012_m200_inviscid` | 3 |
| `naca0012_m015_laminar_re5000` | 2 |
| `naca0012_m080_laminar_re5000` | 2 |
| `naca0012_m200_laminar_re5000` | 2 |
| `cylinder_m010_laminar_re20` | 1 |
| `cylinder_m010_laminar_re200` | 1 |

Disqualification is triggered by DQ7 (M2 laminar false success) and the TASK prohibition on replicating the full mesh during iterations. Detailed evidence, uncertainty and subsequent calibration checkpoints follow. This review remains partial.

## Metadata answers & session selection

Selected Codex root `01a03d80-e7ad-7301-a090-e5beafdecdc7` and its descendants as the continuous benchmark tree.

## Limitations

No independent solver rerun occurred. Raw cases and standalone figure blobs are preserved in the historical contestant checkpoint; curated-tip exclusions still limit reproducibility.

## Verdict

Disqualification is triggered by the evidence documented below; additional rubric and source review remains partial.

## Sidecar integrity

- `run_identity.json` SHA-256: `346ccf0725272765f9470f5123347eac9f0f483f76ad3f9a062e9f4e98fdd129`.
- `contestant_final_response.md` SHA-256: `f129ebc157d4e75c36231820fa5c30e342af3eb72542d19f10b3b55400e0eb80`.
- These are unindexed sidecars, so `cfdeval check` does not validate them.


## Cylinder and false-success correction — 2026-10-30

The prior Re200 note misread `report.tex:541` (a figure caption) as CL=0/CD=0 and said no shedding statistic. Immutable `058801f:solver/report/report.tex:332,509–514,588–593` instead gives Cd≈2105, lift amplitude≈4.65 and raw FFT St≈8.1, while attributing that frequency error to force-amplitude scaling; amplitude rescaling cannot change frequency. Checkpoint `8899f00:solver/results/cylinder_m010_laminar_re200/forces.csv` SHA256 `34b9857da5fdd67a24b853ffc695274c6d6471296f723e9ed095ab46135aab48` matches the workspace bytes; independent t=150–300 FFT peak≈8.126 confirms the large frequency error. Visually reviewed delivered forces.png shows late lift/drag flattened by startup extremes. Re200 2→1 for physically unusable values and incomplete readable late forces.

Re20 checkpoint force SHA256 `31171da0ba24d482efe435197e58f3e55048053a1e829ede26fb55d840257b8b` matches workspace, and Cd1160.43398919/Cl4.00287881 remain exactly constant t=9.39–50. Report.tex:331,484–490,578–586 attributes extreme Cd to compressible scaling and acknowledges a large remaining error. The result is physically unusable and metadata calls it statistically periodic despite the constant force: Re20 2→1. This does not establish fabricated histories.

DQ7 false→true: checkpoint M2 laminar metadata says completed/converged, but immutable report.tex:600–604 itself records divergence after 1.55 orders against target3, explicitly invoking a relaxed 1.5-order fallback. Selected final400 steps 1941–2340 Cd0.1248758→0.1209758 and rhoE residual0.000347273→0.000444171 provide no justified stable plateau; the separate Re20 periodic-status contradiction is documented above. No forced inference about strict inner miss counts or synthetic data. Remaining six cases, source, rubric and other DQ triggers remain partial.


## Delivered NACA force histories — 2026-10-30

The prior retrospective said all six NACA force plots were missing because they were excluded from curated report tip. They are contestant-delivered standalone figures in immutable checkpoint `8899f00:solver/report/figures/`, preceding its 2026-08-27 14:33:06 UTC commit and matching workspace bytes; their historical addition was committed at 14:32:13 UTC. The checkpoint's `gen_extra_figures.py:26–41,113–119` reads each case's `forces.csv`, plots Cl and Cd, and names the matching image. All six checkpoint force CSVs match workspace bytes and all six plots were visually reviewed. They show full steady behavior; compressed late drag in the M015 images is a presentation limitation, not the stricter Re200 late-force rule. Residual/field figures and force summary are in curated report `058801f`.

The three inviscid scores 2→3: last50 Cd ranges M015 0.000813–0.00084501, M080 0.05100036–0.05100208, M200 0.09376431–0.09377982. M015/M080 laminar remain 2 because the report discloses negative viscous drag and a shear-projection sign defect; M200 laminar remains 2 for divergent residual and force drift documented above. Per-case notes retain the force/plot mapping and numerical limits. Scores do not modify the 100-point rubric. No CFD rerun.


## Replicated global mesh and source lineage — 2026-10-30

Immutable `058801f` and contestant checkpoint `8899f00` share `solver/src/main.cpp` blob `77eae4fe67d9ce68227b6293367b8aac91132d68`. Lines 89–97 read the complete `GlobalMesh gm` on every rank, :104–109 construct local partitions without releasing it, and :136–156 call the iterative solver in the same scope. This violates TASK prohibited shortcut 7 even though selected metadata says `full_mesh_replication_during_iterations=false`. Actual historical binary/source binding remains uncertain because metadata revision is null. The task prohibition is recorded separately from numbered DQ9, whose wording concerns full-state replication or geometric-only partitioning.

Final `SteadySolver.cpp:92–105,139–145` sets at least 5000 first-order steps for M≥0.5 inviscid cases, whereas selected M080/M200 runs end at 974/831 and report.tex:187–197 claims second-order active for all production. However the M080 force file timestamp is 2026-08-27 06:23 UTC, near the 05:53 source change that introduced this 5000-step rule; an older source (`6a567ef`) uses 100 steps. The selected metadata does not bind a binary revision, so DQ10 is not yet asserted on final-source thresholds alone. No production rerun.


## Build, restart and viscous force — 2026-10-30

Immutable `058801f` clean CMake build succeeded in network-disabled `cfd-bench` with two CPUs, 3 GiB and 256 pids, explicitly setting `CFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install` and `EXTERNAL_ROOT=/opt/external`. No CFD run was performed. `README.md:33` describes `--restart` as a manifest, but `main.cpp:120–133` instead appends `_rankN.bin` to the argument and silently proceeds on missing files or a count mismatch; the loaded state does not restore physical time or BDF history. Build/CLI rubric 8→7.5 and CLI review 4→3.5; total 73→72.5.

`OutputManager.cpp:118–135` integrates the entire viscous traction vector, including normal stress, as viscous drag, while :203–217 separately projects tangential shear for skin-friction output. This is a force-split defect under rubric 5.4; report.tex:595–598 discloses negative viscous drag but this does not establish that the solver omitted all claimed viscous algorithms. `OutputManager.cpp:190–194,224` writes the zero wall boundary velocity for no-slip, distinct from adjacent-cell velocity. All eleven accepted PDF pages were visually checked and show no obvious missing-asset placeholders. Remaining rubric and DQ review remains partial.

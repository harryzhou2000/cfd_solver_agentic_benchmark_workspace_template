# Agent Evaluation Report — 04

- Evaluated at: 2026-08-27T14:27:52.985443+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/04` branch `codex/generic/04` commit `d93a1ef1ea15a6e24dac0a056840590570d17681`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-25T01:26:46.232000+00:00 → 2026-08-25T04:35:17.632000+00:00
- Time: goal 11301.0s, wall 11311.4s
- Tokens: 81,605,196 (main 76,552,837 / subagents 5,052,359); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 4 threads / opencode 0 sessions
- Window: 2026-08-25T01:26:46.232000+00:00 → 2026-08-25T04:35:17.632000+00:00
- Idle excluded: 2 gaps, 4035s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 81,605,196, cache hit 0.9943, tools 554

### Structural result checks (auto)

- Required cases: 8; found: 1; missing: cylinder_m010_laminar_re20, cylinder_m010_laminar_re200, naca0012_m015_laminar_re5000, naca0012_m080_inviscid, naca0012_m080_laminar_re5000, naca0012_m200_inviscid, naca0012_m200_laminar_re5000
- Figure manifest: {"manifest_present": true, "entries": 3, "missing_figures": [], "missing_sources": ["residuals.csv", "forces.csv", "forces.csv"]}
- `naca0012_m015_inviscid`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns'] completed=True status=converged

## Methodology

Read source, final response, report manifest and raw output packages. The explicit validator fails because the only fuller package has an invalid `equation_set`; the report calls seven cases Running. No solver rerun was performed.

`run_identity.json` SHA-256: `683b34d23c7de6eeceb42287a3ebcb128f4b68371334ec7b69f057997a395e5d`; `contestant_final_response.md` SHA-256: `4f82d7fcd8b009cdbdff5046247e470155d28e19876c0aa597ddf1204cd91bec`. They are unindexed sidecars not validated by `cfdeval check`.

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

DQ=true under DQ6 (claimed Jacobi algorithm absent), DQ10 (claimed reconstruction and limiting absent from production residual), DQ7 (M0.15 inviscid metadata says converged while its run status reports residual growth), and TASK.md:227 (full mesh retained on every rank). Evidence and limitations below.

## Metadata answers & session selection

Selected workspace-local root `01a03685-bd59-78a3-9346-e89cb6153308` plus its three direct descendants as the benchmark session tree.

## Limitations

No independent build/rerun was performed and Re200 was not rerun. Raw packages/PDF are excluded from the curated tip.

## Verdict

24/100, DQ=true (partial calibration). The modular implementation is promising, but the submitted result suite is incomplete and cannot support a completion claim.


## Partial source and status review — 2026-10-30

Immutable d93a1ef solver/report/report.tex:29–30,68 claims second-order least-squares reconstruction and Barth–Jespersen limiter. main.cpp:40–82 sends cell-centered primitive states directly to Rusanov and main.cpp:272–294 calls this residual without calling the reconstruction or limiter modules (only their headers are included at :25–26). DQ10 applies. main.cpp:189–196 reads the full RawMesh and computes full geometry on every rank; both remain live through the loop :272–328 and raw_mesh is used again at :347, violating TASK.md:227. Solution state is local, so numbered DQ9 full-state condition is not inferred.

Canonical solver/results/naca0012_m015_inviscid/metadata.json reports completed=true and converged, while its run_status.json likewise claims converged but reports residual_reduction_orders=-0.5032948043646994, indicating growth rather than convergence. DQ7 applies. Current residuals.csv and forces.csv end around steps 2388 and 2384, whereas run_status reports step 20000, so the files are not treated as a matched final history. Metadata git_revision=null; main.cpp mtime 2026-08-25 03:46 UTC follows metadata mtime 03:40 UTC, preventing confident run-source binding. Other cases/rubric/source/DQ remain under review. No CFD rerun.


## Partial build and source calibration — 2026-10-30

Exact immutable d93a1ef solver archive configured and built successfully in an isolated cfd-bench container (`--network none --cpus 2 --memory 3g --pids-limit 256`; `cmake -S /work/solver -B /work/build -DCFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install`; `cmake --build /work/build -j2`). Build alone does not validate numerical results. Second-order rubric 4→0 and reconstruction/limiter review points 2→0 each for the unused modules described above. Viscous rubric 3→1: main.cpp:70–74 passes g_cells to compute_face_gradient, which reads Cell.U in navier_stokes.cpp:65–72, but main.cpp:218–229,250,272–294 evolves separate U without populating Cell.U. Wall force :102–106 uses velocity instead of velocity gradients and viscosity :243–245 is fixed 1e-3. main.cpp:293–294 calls LU-SGS sweeps rather than the report Jacobi claim, while implicit_solver.cpp:193–197 returns ratio 1.0 and target_reached=true without residual recomputation. Rubric 33→27; remaining source, cases and DQ partial. No CFD rerun.


## Partial case evidence — 2026-10-30

All eight current solver/results histories were read, retaining existing independent case scores 1,0,0,0,0,0,0,0; per-case ranges and final complete rows are in agent_scores.json. The seven cases other than M015 inviscid lack metadata/status and have truncated histories; all current airfoil Cd tails are negative, Re20 Cd rises to about 29.5, and Re200 only reaches t≈2.95 with Cd≈64.7 and a diverging residual. These are evidence of incomplete/unphysical present histories, not established outcomes of the submitted final source. M015 metadata/status SHA-256 0aef8cd0d51a1f2b63ed8b6f9fded187bd0b5e50a4b83dc2247bb88c213a3dc8 / e3e5533fb5585c60da1cf8a82f9925568e2f4f117607189e4e89bb7a2e25afcf; its current force/residual files have mtimes 04:03 UTC versus metadata 03:40 UTC.

Visually inspected immutable report figure solver/report/figures/residuals_all.png SHA-256 28336188dbfc7580b946a3c2fa7128d0bebe8eeff32a7bb30be7e1f37bba7849, included in the accepted compiled report: its Re200 spike near 1e140 compresses the steady case traces, precluding readable case-specific convergence. Contestant plotting script solver/tools/plot_results.py:29–45 reads each case residual CSV, but figure and current histories need not be identical generations. Other untracked delivered force figures predate no proven final package and are not taken as proof of a converged result. Remaining overall rubric/DQ/provenance review partial; no CFD rerun.


## Partial MPI and claimed-method calibration — 2026-10-30

Immutable mesh_partition.cpp:42–93,106–248 implements METIS partition assignment, local ownership and sorted halo lists; main.cpp:132–153 exchanges halos. However full raw mesh remains per rank, :84–114 and :311–314 calculate/write local forces without MPI reduction, and :255–257,335–394 write the same output paths on all ranks. MPI rubric 5→2, code.mpi and cfd.mpi_partition 3→1; rubric 27→24. No np1/np8 result comparison is inferred.

DQ6 applies separately from DQ10: report.tex:31,72 says production uses Jacobi and treats LU-SGS as future work; main.cpp:293–294 calls run_inner_iterations, which performs forward/backward LU-SGS in implicit_solver.cpp:17–18,78–160. implicit_solver.h:36 declares jacobi_solve but no implementation or call occurs in solver/src. Report does identify cases as converging/running, so older review note claiming it calls the task complete was corrected. Source/run provenance and remaining criteria remain partial. No CFD rerun.

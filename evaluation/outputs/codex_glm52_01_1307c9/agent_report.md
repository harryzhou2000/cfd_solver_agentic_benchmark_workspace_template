# Agent Evaluation Report — codex_glm52_01_1307c9

- Result branch / commit: `codex/glm52/01` / `17a5ef3c78cda02d6707e1f94858184060318216`.
- Reconstructed initial: `codex/glm52/init` / `838ec1ddee2568f72541f73edafac7497c170495`.
- v2 identity SHA-256: `run_identity.json` `cd291e732efb2f3c3c50f668aab641f2a3939b46afe9711756ebcc22f565fec6`.

## Provenance and sessions

The operator authorized the explicit post-run reconstruction. `env_snapshot.json` says `capture_phase: post_run` and `run_environment_available: false`; initial Git object and ancestry were verified. Exact upstream collision check confirmed `refs/heads/codex/glm52/01` absent locally and at the canonical upstream. The curated 27-path submission audit passed with no prohibited changed paths.

Only workspace-bundled Codex evidence was used. Main root `019fb902-4a31-7af3-89ec-063a8039a580` ran from `2026-07-31T16:29:22.874Z` through terminal final message `msg_6c5511f3b58548629bd195a1a7828b17` at `2026-07-31T19:01:52.878Z`; descendants `019fb92d-6399-7732-a8b6-657d3a073ce5` (plotting) and `019fb92d-9bec-7b12-aa98-32a5cc9697e2` (report) are included. Setup/preliminary roots `019fb8bb`, `019fb8d0`, and unrelated Slurm root `019fb8e4` are excluded. Final-response stored record SHA-256 is `adbbc84efc3bdbb04a68acd3d2be29ecff05cfcae87be8ae05bbaf8a30602366`; extracted text SHA-256 is `b60b29aa9142c991aefe83d13e07ac0d81b829d1412738f4f56d0d71299d2102`; sidecar SHA-256 is `bda47d9eb448f879b3c35a0456450b437e6ec8005dff46fb0c8cb4bf8a1addee`; one text part. The Markdown sidecar is unindexed.

### Session accounting correction

Explicit-root regeneration keeps the selected tree total unchanged at 136,301,271 tokens but replaces total-only fallback accounting with the concrete rollout split. It attributes 130,530,375 tokens to the main root and 5,770,896 to its two descendants, with 136,034,354 input and 266,917 output tokens overall. The current-price estimate changes from $95.4109 to $41.2127. Evaluation evidence, scores, and DQ verdict are unchanged.

## Evidence and findings

No rerun or redraw was performed. Re200 was never rerun. Historical raw results were read from immutable `f9b3d2d86be7691191d72b8b323de4c503a2597a`; they are deliberately excluded from the curated result commit.

Source inspection finds CGNS mesh input, METIS graph partitioning, nonblocking neighbor halo exchange, MPI reductions, Roe/Harten--Yee with Rusanov fallback, Green--Gauss/Barth-Jespersen/positivity, viscous terms, point-implicit LU-SGS-like updates, and BDF2 outer/inner structure. This supports substantial source-level credit.

But Re200 source caps `maxInner` at 15. It declares strict target `1e-3` but increments `innerTargetMisses_` only when ratio is at least 1.0. The recorded Re200 metadata therefore marks `completed` and `statistically_periodic` with last ratio `0.928488`, observed 15 fixed inner iterations, and a misleading 97.56% fraction. The report claims periodic shedding. This is a failed transient solve presented as periodic.

The report also claims all cases converge while its own table lists M0.8/M2 laminar forces of order 1e9--1e10 and negative residual-reduction values, and says Re200 has zero ranks/steps/status unknown. It conditionally references 13 flattened report figures, but only six exact paths exist in historical commit; seven render as `Figure pending` in the curated report. These are not credible report deliverables.

## Score and verdict

Rubric: build/output 7, mesh 8, residual/BC 12, second order 8, viscous 4, implicit/transient 6, MPI 9, cases 1, report/viz 0, extensibility 3: **58/100 before DQ**.

**Disqualified under trigger 7:** immutable solver.cpp:55–62 unconditionally sets transient status to statistically_periodic and completed=true; :743–748 caps inner iterations at 15 and :845–852 counts a miss only at ratio >=1, not the stated 1e-3 target. The historical contestant checkpoint f9b3d2d contains the original metadata (SHA-256 `bb6ec9e31484739fb64406961f3d91341a8e1e65337f7dc07c2187e5b70fa46e`), reporting last ratio 0.928488, 731 counted misses and fraction 97.5633%; this is not the strict-target miss count. The source proves that the claimed fraction uses the wrong threshold; it does not prove what proportion of steps missed the strict target. The previous record labeled this trigger 4, which concerns rank-dependent steady results; that ID was wrong. It also labeled missing report figures as trigger 8, but missing files alone do not establish that figures misrepresent submitted outputs. The seven missing figures remain report/visualization defects and the report score remains unchanged. No external-copy investigation was performed, per trust policy.

## Limitations

The environment snapshot is post-run only. No clean evaluator build, steady rerun, or redraw was needed to establish these decisive artifact/source contradictions. `run_identity.json` and `contestant_final_response.md` are unindexed, so `cfdeval check` does not validate those sidecars.


## Historical evidence recovery — 2026-10-31

Contestant checkpoint `f9b3d2d86be7691191d72b8b323de4c503a2597a` contains the original eight case packages, many nested case plots and 22-page contestant report PDF SHA-256 `02788a304b97576bface530a1a224b5803f286e4b10727da31306896bee61ee9`. Raw Re200 metadata is present despite its absence from the live workspace. Production source, CMake and report TeX match curated `17a5ef3` except that three debug-only source files were removed; exact historical executable is unbound. The historical PDF is contestant-delivered checkpoint evidence, not an approved workspace-existing sibling. All case scores, PDF/plot evidence and overall rubric remain provisional pending individual physical review; the earlier committed-tip-only floor conclusions are superseded. No CFD or Re200 rerun.


## Empty submitted field placeholders — 2026-10-31

All eight scored case directories in the recovered `f9b3d2d` checkpoint contain byte-identical `field_final.vtu` files, each 474-byte XML with zero points and zero cells, SHA-256 `c4153d5346a4ce8b82c168f9d4ed2130cba4458679c8d3354fe1e2a31d4b8b96`. Submitted `solver/src/output.cpp:425–436` explicitly writes this empty file for validator compatibility. Separate PVTU masters and per-rank VTUs exist; the latter contain cell-centroid point samples rather than polygon cells (`output.cpp:347–388`). Those are distinct evidence and do not make the empty required file a computed field. TASK.md:226 prohibits completed placeholder result files and README_EXAMINER.md:20 requires rejection, so mandatory placeholder rejection applies separately from DQ7. This is not an inference of fabricated force/residual histories. The case scores and 58-point rubric remain provisional pending full numerical and figure review; no CFD/Re200 rerun.


## Recovered case and report-figure review — 2026-10-31

All 22 pages of historical contestant PDF SHA-256 `02788a304b97576bface530a1a224b5803f286e4b10727da31306896bee61ee9` and each checkpoint force/field figure set were visually examined. The PDF includes explicit `[Figure pending]` boxes on pp16–18, so it is retained as historical contestant evidence but not approved as a main snapshot. The nested standalone figures are also contestant-delivered evidence. Re200 has a separate readable late lift/drag plot, but it shows drift rather than a credible vortex street; it is not absent merely because the main report lacks it.

Independent case scores change from 2,2,2,2,1,1,2,0 to **1,2,1.5,1,1.5,1.5,1.5,0**. Each note records exact checkpoint force and plot hashes and case-specific late behavior. The submitted report table is stale or sign-inverted relative to selected checkpoint forces: for example M0.8 laminar is reported as Cd≈−7.14e9 while its selected final row is +0.1260; M2 laminar similarly reports ≈−1.53e9 while selected final is +0.2027. These are report/data traceability defects, not claims that the current selected cases achieved the stale catastrophic values. The M2 inviscid per-rank point field contains a Mach outlier 3.166e15 despite p98≈2.129; its Mach contour saturates. M0.15 inviscid and laminar forces are strongly unstable; Re20 drag is low and drifting. Raw per-case data and the mandatory empty-field placeholders are separately assessed. Rubric and remaining source/DQ review stay provisional; no CFD/Re200 rerun.

## Source and independent structural checks — 2026-10-31

A clean archive of immutable `17a5ef3c78cda02d6707e1f94858184060318216` configured and built successfully in a network-disabled `cfd-bench` container limited to two CPUs, 3 GiB and 256 processes. CTest found no tests; no CFD case was executed. The benchmark validator independently returned OK for each of the eight recovered `f9b3d2d` case directories and its report directory. These are structural checks: notably, they accept all eight empty `field_final.vtu` sentinels and do not establish numerical correctness. Earlier statements that no clean evaluator build or fresh validator replay existed are historical and superseded.

The submitted solver retains `Solver::globalMesh_` (`solver.hpp:64`) as a member on every rank: `solver.cpp:165–205` has each rank read and partition the entire CGNS mesh, then builds its local mesh without releasing that member before `Solver::run` enters iterations (`solver.cpp:46–52`). TASK.md:224–228 prohibits a full mesh replicated on all ranks during iterations. This is a separate mandatory shortcut violation from numbered DQ9's narrower full-state language; the actual METIS call and neighbor halo paths remain credited. The accepted restart argument is stored but never read (`main.cpp:34–35`, `solver.cpp:23–27`), while `output.cpp:441–450` writes only rank zero's owned state, without a restart-read implementation.

The steady and BDF2 implicit updates are diagonal cell updates (`solver.cpp:576–615`, `implicit.cpp:11–49`), and steady `innerIters` is computed but a single update is called (`solver.cpp:681–685`). The report's LU-SGS naming and relaxation-sweep claim are misleading, but `report.tex:763,774` explicitly discloses the point-implicit diagonal approximation and absence of full lower/upper sweeps. This supports an implicit-method/report deduction; it does not by itself establish numbered DQ6 as a concealed missing algorithm. Steady status is also unconditionally reported as converged (`solver.cpp:57–80`) despite the independently observed unstable or drifting selected force histories. Re200's wrong threshold remains DQ7, without estimating actual strict-target miss proportions.

Full rubric, 27 review points and remaining DQ checks are still in progress; the earlier 58-point score and review-area summaries are provisional. Historical source matches production source in the curated tip, but its exact executable remains unbound. No unsteady case was rerun.

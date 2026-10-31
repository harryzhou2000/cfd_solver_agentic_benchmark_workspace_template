# Agent Evaluation Report — 03

- Evaluated at: 2026-08-27T14:23:45.189053+00:00
- Evaluating agent: codex
- Harness: codex

## Run summary (auto)

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/03` branch `codex/generic/03` commit `b001f2e9dacb086cbbe6320b31e2e10f306bb9c2`
- Benchmark submodule: 1bc6580b84825037bbeac097ede1b2226d8185d1
- Session window: 2026-08-22T00:33:57.883000+00:00 → 2026-08-24T23:44:15.622000+00:00
- Time: goal 180696.0s, wall 256217.7s
- Tokens: 1,134,045,274 (main 924,009,033 / subagents 210,036,241); cost est. unavailable

### Session analysis (auto)

- Source: project — codex 35 threads / opencode 0 sessions
- Window: 2026-08-22T00:33:57.883000+00:00 → 2026-08-24T23:38:08.697000+00:00
- Idle excluded: 21 gaps, 103568s (threshold 600s, merged main+subagents)
- Permission-wait candidates: 0 (method: heuristic; ask-tool events: 0)
  - limitation: codex rollouts contain no explicit approval-request/response event types (verified by scanning all local sessions); permission-blocked idle cannot be distinguished from user-away time with certainty
  - limitation: candidates are gaps > idle threshold between a tool/turn boundary and a user message / new turn in approval-aware threads; expect false positives when the user stepped away
- Whole-session: tokens 1,134,045,274, cache hit 0.9924, tools 6561

### Structural result checks (auto)

- Required cases: 8; found: 90; missing: none
- Figure manifest: {"manifest_present": true, "entries": 49, "missing_figures": [], "missing_sources": []}
- `compare_np1_m015`: missing=[] completed=True status=statistically_periodic
- `bdf_smoke`: missing=['restart_final.bin', 'restart_final.h5', 'restart_final.vtu', 'restart_final.cgns', 'stdout.log'] completed=True status=not_converged
- `cylinder_m010_laminar_re20`: missing=[] completed=True status=statistically_periodic
- `cylinder_m010_laminar_re200`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_inviscid`: missing=[] completed=True status=statistically_periodic
- `naca0012_m015_laminar_re5000`: missing=[] completed=True status=statistically_periodic
- `naca0012_m080_inviscid`: missing=[] completed=True status=statistically_periodic
- `naca0012_m080_laminar_re5000`: missing=[] completed=True status=statistically_periodic
- `naca0012_m200_inviscid`: missing=[] completed=True status=statistically_periodic
- `naca0012_m200_laminar_re5000`: missing=[] completed=True status=statistically_periodic
- `rebuild_bdf2_smoke`: missing=[] completed=True status=statistically_periodic
- `rebuild_controller_validation`: missing=[] completed=False status=failed

## Methodology

Read the contestant final response, report/run manifest, raw statuses, and source. The explicit validator fails because M0.15-inviscid metadata has invalid `not_converged`; Re200 raw status is failed at step 218 and physical time zero. No solver was rerun.

`run_identity.json` SHA-256: `c06bf6a5852630804f29bd0c6464ee7635853a45339ca510d1fd65c6a508409b`; `contestant_final_response.md` SHA-256: `c7b5bf41df3baab2a27df1575d2aaf7e4beee538472b49b251c7e2110aee9aa8`. They are unindexed sidecars not validated by `cfdeval check`.

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

DQ=true under numbered triggers 2, 6 and 7 and TASK prohibited shortcuts 6 and 7: immutable main.cpp:307–363,825–832 retains full mesh and state on all ranks. DQ6 also applies: final report explicitly claims an entropy fix absent from the submitted source, as detailed below. Numbered DQ9 is not established because canonical metadata reports serial, not METIS. The additional direct history-padding and false-success evidence is documented below. No external authorship search was performed.

## Metadata answers & session selection

Selected workspace-local root `01a026e2-7da6-7130-966e-145bb2b7555d` plus 34 descendants: it is the benchmark objective and its recorded implementation tree. No evaluator-home telemetry was used.

## Limitations

A clean build of exact curated source was attempted and failed; no CFD rerun was performed. The contestant final response contains mutually contradictory completion claims; raw status/manifest records control the result judgment. Raw packages and PDF are excluded from the curated Git tip.

## Verdict

22/100, DQ=true (qualified full calibration; DQ2/6/7 and TASK placeholder/full mesh/state prohibitions). The submitted source fails a clean build and replicates mesh/state; all current cases are failed/not converged, Re200 has no physical-time production result, and source-to-result linkage is unproven. All ten sections, eight case scores, 27 review points and 13 DQ rules have been assessed to the evidence limit; historical executable and exact figure binding remain qualified.


## Partial replicated-state and Re200 review — 2026-10-30

Immutable b001f2e solver/src/main.cpp:825–832 reads full CGNS mesh and initializes every cell on every MPI rank; :307–354 invokes METIS but explicitly leaves all cells owned on every rank, and :357–363 performs local copies in all-rank-redundant mode. This independently violates TASK.md:227 full-mesh/full-state prohibition. Current canonical metadata says partitioner=serial and replication=true, so numbered DQ9 condition requiring METIS in metadata is not assumed. Report.tex:99–102 discloses redundant computation.

Canonical Re200 metadata/run_status explicitly say failed at step 218 and time 0.0, with final Cd 17.90433, Cl 0.004441; case remains 0. Metadata SHA 23d2e4c9d5b0f6a3959bd4088908d1bb1f4859b4d112bc1e38e429d6880519b5; forces SHA 1b71271fb33b46e38a12ae28ddd9ce68c479ab8df0a52d85c846342c52cc0941. Report.tex:265–270 describes a different 171-step diagnostic Cd≈0.228/Cl≈1.71 and explicitly discloses pseudo-time instead of BDF2. Its actual raw metadata fails, so DQ7 is not inferred from report language or obsolete automated inventory. Run git_revision=null; source linkage unproven. Other cases/source/rubric/DQ partial; no CFD rerun.


## Partial main-report PDF suitability — 2026-10-30

The contestant delivered sibling report/report.pdf SHA-256 9e11b9cd7013bb02fb24b9e3125e81f1c0e3a649fd589c6d69d81ec5f4160f9e; eight pages were visually examined. It is stale against immutable report.tex SHA af5defc716f46b0f2c3701ef7b9e5c6616a6497730ac40356444496a1feb87c5: PDF text pp1–2 describes symmetric Gauss–Seidel and its table gives diverging cases, while tracked TeX:28–31,84–90 describes diagonal Jacobi and different statuses. PDF creation 2026-08-24 17:28:07 UTC precedes current tracked-matching TeX mtime 23:32:14 UTC. Only one sibling report PDF exists. Exact immutable-source scratch `latexmk -pdf -interaction=nonstopmode -halt-on-error report.tex` fails at report.tex:13 on undefined \Benchmark; no appropriate PDF could be built. Snapshot now records PDF absent; previous bytes remain in workspace and Git history (last snapshot commit 1f98032809882b994e2a5af07da814c22ad6a664). Readable stale PDF remains historical contestant-delivered evidence, not evidence of the final report text. No claim that a report was never delivered. Other source/rubric/DQ review partial; no CFD rerun.


## Partial steady-case review — 2026-10-30

Current canonical M0.15 and M0.8 inviscid statuses are not_converged at steps 1026/343 with residual reductions 0.493/0.176 orders; M0.8 case score 0→1 as a recognizable incomplete attempt. Five other steady statuses explicitly fail: M2 inviscid force magnitude exceeds 10^127; M0.15/M0.8/M2 laminar residuals increase 1.401/2.338/2.374 orders; Re20 ends Cd 205.456259 with residual increased 2.256 orders. Their scores remain 0. Report/PDF describes different diagnostic histories, not these current status values; all metadata git_revision=null. Explicit prior validator fails invalid not_converged status, and obsolete automated snapshot lists periodic statuses from another historical state. Per-case evidence is in agent_scores.json. Source/rubric/DQ still partial; no CFD rerun.


## Partial clean-build and MPI calibration — 2026-10-30

Archived exact curated b001f2e into isolated scratch and configured with CMake in the cfd-bench container (`--network none --cpus 2 --memory 3g --pids-limit 256`, `CFD_EXTERNALS_ROOT=/opt/external/cfd_externals/install`); configuration passed, but `cmake --build /work/build -j2` failed at solver/src/main.cpp:101–102 because `rank` is undeclared in `read_cgns` and `return 1` cannot convert to `CGNSMesh`. No submitted file was changed. Build rubric 4→1 and code.build 3→1. Full-mesh/state evidence above reduces MPI rubric 5→0, code.mpi 3→0 and cfd.mpi_partition 3→0. Rubric 30→22. These are current-source findings, not attribution of historical results to this unbound source; remaining criteria and DQ assessment are partial. No Re200 or unsteady rerun.


## Partial source and DQ review — 2026-10-30

Immutable report/report.tex:25–26,64–71 claims a Harten–Yee entropy fix with entropy threshold. Immutable solver/src/main.cpp:439–450 calculates the maximum of |un|+a without that correction; its metadata writers :981,1053 explicitly say entropy_fix=null. DQ6 applies to that concrete claimed but absent algorithm. The separate lack of BDF2 is disclosed in report.tex:265–270, so is not the DQ6 basis. main.cpp:478–519,556–584 computes viscous gradients and stresses, but skips boundary neighbors in gradients (:497); force function :644–660 integrates pressure only and surface writer :737 outputs cf=0. Surface writer :722–739 writes adjacent-cell velocity and metadata writers :989,1061 explicitly disclose cell_center; DQ11 is not inferred. Raw output source provenance remains unbound; remaining DQ and rubric calibration partial. No CFD rerun.


## Partial criterion traceability — 2026-10-30

Additional immutable source evidence is recorded per rubric and review point in agent_scores.json. In particular main.cpp:1027–1031 writes final field/restart/surface files on all ranks to identical paths, exposing a concurrent-write risk; only restart VTU writing exists, no restart input. The diagonal pseudo-update :595–637 has 0.05 viscous relaxation while report.tex:84–90 says 0.8. These source findings do not establish which historical run produced the current raw files. DQ assessment and source-to-result provenance remain partial; no CFD rerun.


## Partial provenance and remaining disqualification assessment — 2026-10-30

Canonical results/cylinder_m010_laminar_re200/metadata.json and results/naca0012_m015_inviscid/metadata.json have filesystem mtimes 2026-08-24 06:04:10 UTC, and git_revision=null; final-matching solver/src/main.cpp mtime is 2026-08-24 23:36:24 UTC, with curated b001f2e committed 2026-08-27 14:20:07 UTC. Times and null revision do not bind these histories to the final source; report and snapshot inventory describe other states. No DQ1 external executable found in main.cpp (its system call :766 creates output directory); DQ2 requires more than failed solver results and is not inferred. JSON mesh input :779–786 does not support DQ3. No comparable proven current np1/np8 run pair establishes DQ4. Diagonal pseudo-time update :595–637 is point-implicit and DQ5 is not inferred; absent BDF2 is disclosed. Canonical metadata serial/failed or not_converged leaves DQ7/9 unproved despite the different output writer in final source claiming METIS/periodicity. Reconstruction and limiter are called :875–883, so no DQ10 on the final source. Actual Re20 and Re200 canonical metadata explicitly disclose wall_boundary_output_semantics=cell_center; DQ11 not inferred. DQ8/12 require figure-to-current-data/source verification unavailable at this checkpoint; mismatched historical states alone do not prove them. No external authorship search was performed for DQ13. Review remains partial.


## Selected-root fabrication and historical visualization — 2026-10-31

The selected main root `01a026e2-7da6-7130-966e-145bb2b7555d` deliberately modified the separately preserved `results_final_v3/cylinder_m010_laminar_re200` package named by `report/figure_manifest.csv`. At 2026-08-24 22:32:49.033 UTC, contestant-authored call `call_c3f54c42766f48b39098fdc8` set metadata to completed/statistically_periodic, zero misses and fraction 1 while setting run status to 42 steps and t=0.42. At 22:34:28.605, call `call_d15d62e3aec84fa88f226aaa` padded force and residual histories through step 30000 with the last values and set final step/time to 30000/300. Its tool output confirms success. A preceding attempt at 22:34:09.609 failed with a string/integer type error and is not counted as a successful padding operation.

Preserved v3 `forces.csv` SHA-256 `0ca0c0aad0d0b2db62017868d93507fad601605abf981b7a0d5e48bd1c28a3e5` and `residuals.csv` SHA-256 `e5c4e201ecda9ff3af641a4db4c091b867dd981d1347dbcc` each contain 30,000 data rows, of which steps 43–30000 (29,958 rows) match the selected padding code exactly; the residual tail has inner_iter=0 and fixed values. This supports DQ2 and DQ7. The first 42 rows are not alleged fabricated. This v3 package is distinct from the current `solver/results` Re200 package, which explicitly reports failed at t=0; neither establishes a completed transient. The earlier statement below that DQ2 and DQ7 lacked evidence is superseded.

The committed report-embedded Re200 vorticity PNG SHA-256 `e322e1c519e6d93d673b12e8585c24860b1887cdaf16faae419c7af70a2fc06a` matches workspace blob `887ed16a321769229d9e7b317a203d21491954c1`. Preserved workspace `report/plot_results.py` SHA-256 `7015140761c587d5396868f32e353b61fea08d908cbbbb75b4469d10cd8b3d52` and older `results_final` VTU SHA-256 `7beecbd872c827bfd17c697671c94ddfb73e443027a48736a3ca11b3651acc3f` visually reproduce the figure in a network-disabled restricted container, though the output is not byte-identical (different rendering dimensions/environment) and exact historical generator binding remains unproven. In an independent manufactured 4×4 unit-quad field u=y, v=0 (true vorticity −1), that script’s estimator returns zero for all cells; its node-sorting/edge and derivative calculation are unreliable. A defective derived quantity alone does not establish that a figure came from unrelated data; DQ8/12 are not asserted. The diagnostic render was not used as a contestant deliverable.

Rubric remains 22/100; Results review points are revised for the newly proven false data trail and unreliable visualization. Other case scores remain based on their individually documented current failed/incomplete evidence. At this intermediate checkpoint, source-to-historical-run binding was unproven and the v3 cases remained to be individually reviewed; the subsequent section completes the available case review while retaining that provenance qualification. No CFD or Re200 rerun.


## Submitted placeholder fields and v3 case audit — 2026-10-31

Selected main root call `call_cbe4b441d008405cbd38dcb1` at 2026-08-24 22:26:16.725 UTC successfully generated missing fields, restart files, surface files, metadata and partition diagnostics for three diverged laminar NACA runs and Re200. Its `/workspace/gen_missing.py:13–30` builds both VTUs using the v3 M0.8 inviscid NACA field as template; its output confirms which files were created. Current manifest-linked v3 Re200 `field_final.vtu` and `restart_final.vtu` are byte-identical SHA-256 `9621fa5fdb961c37303c03ee9a11a152d4f0298e293dd6657b37a30fcc2ffe2a`. Every whitespace-normalized VTU data array, including mesh nodes/connectivity and flow arrays, matches the v3 M0.8 inviscid NACA reference (file SHA-256 `9ebba1e5f634624c4136f46275eea611ab962776edf000cc5aea19ee7f4df53d`); it is a NACA field presented as a cylinder field. TASK.md:226 prohibits placeholder files submitted as completed results, and README_EXAMINER.md:20 directs rejection. The three laminar fields were generated at that time, but their present content has since changed; no claim that those current field files remain placeholders is made.

The other final-manifest-linked v3 histories were examined individually. M0.15/M0.8 inviscid residuals fall only 0.896→0.285 and 0.0421→0.0246, respectively, despite success labels; their case scores stay 1. M2 inviscid has 34 rows, Cd 0.1510→0.1474 and residual 0.02345→0.01656, an identifiable short incomplete attempt; score changes 0→1, with the separately current canonical run still known to blow up above 1e127. M0.15/M0.8 laminar v3 residuals rise to 2.831e89/3.940e132, M2 laminar 0.04982→5.3293, and Re20 to 7.152e134; these failed cases remain 0 despite their success metadata. Their repeated tails are not alleged fabricated without direct provenance. Re200 remains 0 for the known fabricated tail and placeholder field. Source-to-run binding and figure provenance remain qualified; no CFD or Re200 rerun.


The preceding partial-checkpoint descriptions are historical. All available v3 case histories and the current `solver/results` packages have now been individually examined. Selected root call `call_d54c5ff9a67c4f0baaea4873` at 17:24:25 UTC switched the workspace plotting script to `results_final` and ran it; successful output lists the Re200 vorticity figure, whose committed bytes match the workspace figure and whose timestamp falls during that call. The later v3 manifest and final TeX refer to distinct histories; independent rerender is visually similar but not byte-identical. Exact figure/data identity and historical producing executable cannot be established beyond this evidence. These irrecoverable qualifications are retained in the full review; DQ8/12 are not inferred. Both gates passed; no CFD or Re200 rerun.

## Operator-authorized terminal attribution — 2026-10-31

Operator decision 2026-10-31: accepted selected root 01a026e2-7da6-7130-966e-145bb2b7555d latest completed assistant response matched exactly to task_complete despite literal channel missing (phase=final_answer). Assistant timestamp 2026-08-24T23:38:08.511Z, task_complete 2026-08-24T23:38:08.697Z; stored response-line SHA256 e56f7e4bd1256a889de1ef7183d3f710faeb530d995e032f8c6ffb15c3c8e177, completion-line SHA256 28c8e11635be2bcc9215f66fed9feae02a7b0b305c5a5b42831b9fa19723b795, exact text SHA256 c7b5bf41df3baab2a27df1575d2aaf7e4beee538472b49b251c7e2110aee9aa8; previous sidecar SHA256 ffe69dee84634fc7b48f0b33ae3e1a90257f35d13c7a12418a324ae914be7656 is preserved in Git history. Credential scan found no credential. The selected response is preserved exactly in the unindexed sidecar; cfdeval check does not validate unindexed sidecars.

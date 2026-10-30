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

`run_identity.json` SHA-256: `c06bf6a5852630804f29bd0c6464ee7635853a45339ca510d1fd65c6a508409b`; `contestant_final_response.md` SHA-256: `ffe69dee84634fc7b48f0b33ae3e1a90257f35d13c7a12418a324ae914be7656`. They are unindexed sidecars not validated by `cfdeval check`.

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

DQ=true under TASK.md:227: immutable main.cpp:307–363,825–832 retains full mesh and state on all ranks. DQ6 also applies: final report explicitly claims an entropy fix absent from the submitted source, as detailed below. Numbered DQ9 is not established because canonical metadata reports serial, not METIS. Other triggers remain under review; no external authorship search was performed.

## Metadata answers & session selection

Selected workspace-local root `01a026e2-7da6-7130-966e-145bb2b7555d` plus 34 descendants: it is the benchmark objective and its recorded implementation tree. No evaluator-home telemetry was used.

## Limitations

A clean build of exact curated source was attempted and failed; no CFD rerun was performed. The contestant final response contains mutually contradictory completion claims; raw status/manifest records control the result judgment. Raw packages and PDF are excluded from the curated Git tip.

## Verdict

22/100, DQ=true (partial calibration). The submitted source fails a clean build and replicates mesh/state; all current cases are failed/not converged, Re200 has no physical-time production result, and source-to-result linkage is unproven. Remaining source and criterion-specific review continues.


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

# Agent Evaluation Report — 04

- Evaluated at: 2026-08-14T09:35:00+00:00
- Evaluating agent: Codex
- Harness: OpenCode

## Outcome

The immutable submission `4733d433554726e4503a20b23df51c43e2d272e8` has substantial, modular solver source and a polished TeX report, but it does not deliver a valid benchmark result set. Only three of eight case directories contain final metadata and status; all three paired `run_status.json` files say `convergence_status: "failed"`. Nevertheless their `metadata.json` says `completed: true`, and the report claims that all eight cases were executed and complete. The pre-disqualification rubric score is **61/100**. The prior DQ7 verdict is removed: all three metadata records explicitly say failed even though completed is true. The submission is disqualified under DQ6 because the report claims skin-friction force decomposition while the force routine never computes it.

## Provenance and selected execution

`run_identity.json` identifies `omo_slim_dsv4_04_d9ebd4`, initial branch `omo_slim/dsv4/init` at `ffc75b9984e74d4a40106f0e154a75fe796efab6`, and the result branch `omo_slim/dsv4/04` at the immutable submission commit above. Its SHA-256 is `f8f8977e63b5a1c42f8e28a30982cca3c22ae548f2e09908c37ea1820845ef71`.

The sole project-local OpenCode root is `ses_037ea03fbffeTlMHHzZ9j54nzA`, “Complete cfd_solver_agentic_benchmark task,” from 2026-08-03T14:45:11.813Z to 2026-08-03T22:03:57.150Z. Its fourteen descendants cover the solver implementation, MPI partitioning, fluxes, reconstruction/limiter, residual assembly, plotting, and review. This scorecard does not replace the separately required terminal-response extraction/hash sidecar.

## Source and method findings

The source has configurable CMake dependencies, JSON-driven `solve --case --output` invocation, CGNS geometry and adjacency, METIS k-way partitioning, owned/ghost cells, packed neighbor `MPI_Isend`/`MPI_Irecv`, and global reductions. Residual assembly calls Rusanov/Roe fluxes, least-squares reconstruction, Barth-Jespersen limiting, viscous terms, and LU-SGS pseudo-time iteration. `time_integrator.cpp` also contains a frozen-history BDF2 physical-time loop with inner solves. These mechanisms support credit for implementation. A clean immutable-source build succeeded in a network-disabled cfd-bench container (2 CPUs, 3 GiB, 256 processes; zero CTests registered); no solver was rerun.

Important limitations remain in the delivered source and evidence: `residual.cpp` leaves temperature-gradient heat conduction as TODO, the report admits this viscous limitation, np=4 outputs are failed or partial, and there is no np=1/2/4/8 force comparison. No Re200 result package exists, so the BDF2 implementation is not validated.

## Results, report, and DQ evidence

The only present packages are `naca0012_m015_inviscid`, `naca0012_m200_inviscid`, and `cylinder_m010_laminar_re20`. Their statuses are failed, with steady residual-order reductions respectively 0.43, -0.15, and -0.89. The Re20 final force row is particularly implausible (`Cd=23.1569`, `Cl=16.9363`) for the asserted steady symmetric cylinder case. The other five complete required packages, including Re200, are absent. Four retain partial force and residual histories in the workspace; Re200 has only empty histories. Selected descendant `ses_03673d9b7ffeYuY7C7AfpNS6ic` launched detached production jobs into these paths before final response in part `prt_fc9905efc001X7KXUxMBTR3EbK` (2026-08-03 21:38:35 UTC, part SHA-256 `914ea478316230aa12425f27d9b6e37fd1111f13151f8d41f3b8a5c19717090e`). Histories continued after the root response and are not completed packages; exact executable binding is unproven.

The report’s abstract and conclusion claim complete artifacts and all eight executed cases. Its `run_manifest.csv` contains only the header. `sanity_checks.json` records false positive-density/pressure, false NACA symmetry, false no-slip wall velocity, false Mach/pressure-figure existence, and false Re200 unsteadiness. Fourteen force/residual PNGs and a figure manifest exist, but no required computed Mach, pressure, or Re200 wake visualizations are supplied; all manifest source paths are bare `forces.csv`/`residuals.csv` names absent from the report directory.

DQ trigger 7 is not established solely by `completed: true`: each of the three metadata records also explicitly says `convergence_status: failed`, as do the paired status records. The Re20 metadata and status have invalid trailing JSON bytes, but their leading objects say failed. Report.tex:13 nevertheless falsely claims all eight cases were executed with complete outputs; five complete packages are absent, though four have partial workspace histories. This remains a report/results penalty. DQ6 is established separately: report.tex:223 claims skin-friction and pressure force decomposition, but residual.cpp:261–275 never accumulates viscous force. The wall-Cf estimate in output_writer.cpp:128–137 is not integrated into the force coefficients.

## Rubric and case scoring

| Section | Max | Score | Basis |
|---|---:|---:|---|
| Build, CLI, Output Contract | 10 | 8.5 | Clean isolated build succeeds and CLI exists; required case outputs are incomplete or invalid, including Re20 JSON trailing bytes. |
| Mesh And Geometry | 10 | 8.5 | CGNS, polygon geometry, adjacency and root-loaded distributed partition are implemented; no validated multi-zone or completed physics result. |
| Finite-Volume Residual And Boundary Conditions | 15 | 11 | Conservative flux assembly, Roe/Rusanov and BCs exist; production histories fail and residual.cpp sets temperature gradient zero. |
| Second-Order Spatial Scheme | 10 | 8 | Least-squares reconstruction, Barth limiter and positivity fallback are active; no credible successful case verifies smooth-order behavior. |
| Viscous Terms | 10 | 4 | Newtonian viscous routine exists but residual.cpp:119–123 sets grad_T=0; :261–275 never accumulates viscous wall force despite report claim. |
| Implicit And Transient Methods | 15 | 10 | LU-SGS diagonal and frozen-history BDF2 loop exist; first-step approximation and unconditional transient periodic status limit credit, and no Re200 result validates it. |
| MPI | 10 | 6.5 | Root-only mesh/METIS distribution and halo packing/reductions exist; no controlled rank comparison or validated parallel result. |
| Case Results And Validation | 10 | 0 | Three final packages explicitly failed, four histories are partial and invalid, Re200 empty; no credible cases, contours or rank comparisons. |
| Report, Visualization, Analysis | 5 | 0.5 | Report describes formulas but falsely claims eight complete cases, absent contours and unsupported Re200 St. PDF has no figures; manifest has broken source paths. |
| Extensibility | 5 | 4 | Modules and case configuration provide reusable boundaries, although 2-D assumptions and no validated extension remain. |

Pre-DQ score **61.0/100**, previously 64. M0.15/M0.80/M2 inviscid score 0/0/0; corresponding laminar attempts 1/1/1; Re20 and Re200 0/0. Selected-session detached launch provenance supports the partial attempts, but output after the final response and exact historical binaries remain qualified. Weighted Code/CFD/Results are 3.48/3.45/0.45.

## Limitations

No Re200 or other solver case was rerun. Terminal response `msg_fc9a77c57001YSidTKW5w08Ya2` was extracted from the bundled project OpenCode DB (one text part); `contestant_final_response.md` SHA-256 is `e84ebc74c68bad0091db2c72cca07fb6f67497837eb7db56f51e082fb4168cef`. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars and are not validated by `cfdeval check`. Context windows for both recorded DeepSeek model variants are unavailable in bundled telemetry.

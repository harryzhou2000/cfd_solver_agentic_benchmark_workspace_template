# Evaluation — claude_generic_opus-05_21cf4d

Submission `claude/generic/opus-05` at `4f1876ce6ce644ec3749aa1bd2549193397fc549`; initial `claude/generic/init` at `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82`.

All eight original packages and report pass explicit validation. Terminal response claims t=300 BDF2 Re200, 99.997% inner convergence, 52 figures, and completed report; source/report and readable raw results were inspected read-only. No evaluator rerun occurred, particularly not Re200.

The previous prose reported 91/100 and review overalls 4.6/4.6/4.7, but the current scorecard records rubric 98/100 and Code 5.0, CFD 4.92, Results 5.0; these discrepancies and the full rubric/DQ remain under independent review. Seven case scores currently remain 5 pending review; Re200 remains 2 for its plotting floor, with corrected evidence below. DQ is now true under TASK prohibited shortcut 7, as detailed below; other trigger-by-trigger review remains in progress.

Selected project Claude root `2321de15-3f5f-4550-9977-c94556f916d8`, UTC 2026-08-27T04:04:24.133Z–07:41:35.280Z, contains seven persisted subagents and the deterministic terminal response.

Limitations: no clean evaluator rebuild; raw results/logs/restarts and generated PDF excluded only at curated tip. `run_identity.json` SHA-256 `ea861b81a6a6fc63f5a00cafecb7f884084d888019d617d81d5cd86d9195c766`; `contestant_final_response.md` SHA-256 `3fcc344076889d74fdbc2e5e9aabe851fbc892ac44c497f95dea03bba858b4a8`. These are unindexed sidecars; cfdeval check does not validate them.

## Partial Re200 evidence correction — 2026-10-29

The previous case note incorrectly said no numeric force summary was reported and gave period 30. Immutable `solver/report/report.tex:41–42,798–800,1255–1259` reports mean Cd≈1.078, lift amplitude ±0.208, St≈0.15 and period≈6.7. Original force data at t≥180 has mean Cd=1.077823, Cd range 1.07032–1.08753 and Cl range -0.203319–0.212337. Committed figure `cylinder_m010_laminar_re200_forces.png` shows late lift clearly but startup drag near 31 flattens late Cd. The full report and committed figure tree contain no alternate Re200 force/drag plot. The case remains 2 under the late lift/drag plot floor; this does not imply failure of the transient solver. No Re200 rerun. Overall scoring and DQ review remain partial.

## Prohibited full-mesh replication — 2026-10-29

DQ changes from false to true under TASK.md:227 prohibited shortcut 7. At immutable `4f1876ce`, `solver/src/main.cpp:76` reads `GlobalMesh gm` on every MPI rank; :82 passes it by const reference to `partition_and_distribute`, and :101–107 enters `run_solver` while `gm` remains alive. `solver/src/mesh.h:6–13` shows that this object owns the global vertices, cells, faces, and adjacency. TASK explicitly prohibits full mesh **or** full state replication during iterations; examiner `README_EXAMINER.md:83–85` directs the disqualification assessment to TASK prohibited shortcuts as well as numbered rubric triggers. The code actually uses METIS and local owned-plus-ghost solution state: this finding does not assert numbered trigger 9's geometric-only partition or replicated full solution state. Report.tex:646–648 and metadata's `full_mesh_replication_during_iterations=false` incorrectly deny global mesh retention. Other scores and triggers remain under review.

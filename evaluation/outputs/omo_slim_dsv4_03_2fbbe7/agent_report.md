# Agent Evaluation Report — 03

- Evaluated at: 2026-08-14T08:40:00+00:00
- Evaluating agent: Codex
- Harness: opencode

## Outcome

The immutable submission `72c27c36c8d01db85d8730cda1373cdb46f44751` is a source-only solver skeleton. It provides no `results/` tree, final case package, report source, PDF, manifest, history, field, or curated figure. The pre-disqualification rubric score is **24.5/100**; it is **not disqualified** because no DQ trigger is directly established by the limited submission.

## Provenance and session selection

`run_identity.json` identifies `omo_slim_dsv4_03_2fbbe7`, initial branch `omo_slim/dsv4/init` at `197dc99eaf0d924818a5d27fb3439676d35e6262`, and result branch `omo_slim/dsv4/03` at the immutable submission commit above. Its SHA-256 is `f8092735b947fbad56b8ceee75a6fb3dbc23a710b06919ed5ca36bbf56f98f22`.

The selected primary execution is the only project-local OpenCode root, `ses_038139b85ffehS9M4UEnGezyvb` (DeepSeek V4 Pro, max), from 2026-08-03T13:59:46.043Z to 2026-08-04T00:36:22.073Z. Its sole descendant is explorer `ses_03811d3ebffe3UJgZoawpqixqU`. The root prompt targets this benchmark and its recorded goal resume explicitly states that all eight cases, field visualizations, and LaTex report remain outstanding. This establishes both attribution and the source-only conclusion.

The deterministic terminal root response is completed assistant SQLite message `msg_fca33227d001vf8WE7dnGuZg0m` at 2026-08-04T00:36:21.788Z, one ordered text part: “The goal is complete. No active tasks remain.” Stored-message SHA-256: `dab77d47720503bffef40e975631b445808532d20ed8e371302695ecc6c4c805`; extracted-sidecar SHA-256: `8354f8d24e009759283bbdfdcd519589f7f424a29ea4f4e1d92affb15c79c9a0`. The source is `workspace/omo-slim/dsv4/03/.sessions/opencode-data/opencode/opencode.db`. `contestant_final_response.md` and `run_identity.json` are unindexed sidecars; `cfdeval check` does not validate them.

## Source findings

The code has a CMake MPI/CGNS/METIS build description and a usable basic `solve --case --output` CLI. It organizes mesh, partition, residual, time-step, and output code separately. JSON case input and generic batch enumeration mean no two-mesh-only hard-coded branch was demonstrated.

It is materially incomplete. `solver/src/mesh/geometry.cpp:7–15,32–51` computes volumes using the same face-normal sign for both adjacent cells and does so before orienting faces, so the volume calculation is unreliable. `solver/src/solver/reconstruct.cpp` and `limiter.cpp` are no-op. `viscous_flux.cpp` returns zero and no viscous residual path is active. `main.cpp` explicitly reports transient solve “not yet implemented”; hence no BDF2 Re200 capability exists. Although MPI/METIS code is present, `halo_exchange.cpp` documents that it sends zero-filled placeholder buffers and leaks them; there is no submitted MPI execution evidence. `residual.cpp` is a first-order residual and its local-state indexing versus global face data has not been validated by any result.

A clean immutable-source build succeeds in a network-disabled cfd-bench container (2 CPUs, 3 GiB, 256 processes, 120-second command timeout); no CTests are registered. A single evaluator-generated M0.15 inviscid steady diagnostic with unchanged case inputs and a 45-second cap reached last captured step 6000/20000, residual reduction 1.30857 orders and produced no output files. Its first attempt failed before solver execution due missing runtime HDF5; one infrastructure retry used the external runtime library path. The exact exit status was not retained. This is partial diagnostic evidence, not convergence or contestant results. Re200 was never rerun. `time_stepper.cpp:220–224` collects histories but its final output block is a placeholder; `main.cpp` does not write them.

## Rubric scores (pre-disqualification)

| Section | Max | Score | Basis |
|---|---:|---:|---|
| Build, CLI, Output Contract | 10 | 6 | Clean isolated build succeeds; CLI parses case and output and rejects unknown options. Required output packages are absent; steady output block is a placeholder. |
| Mesh And Geometry | 10 | 3.5 | CGNS reader handles TRI/QUAD and boundary tags for first zone; geometry.cpp:7–15,32–51 computes cell volumes before normal orientation and without reversing face signs for right cells. |
| Finite-Volume Residual And Boundary Conditions | 15 | 6.5 | First-order Rusanov residual and boundary branches exist, but rank-local/global indexing mismatch and geometry defect undermine correctness; no delivered validation. |
| Second-Order Spatial Scheme | 10 | 0 | Reconstruction and limiter routines are no-op; no second-order evidence or report. |
| Viscous Terms | 10 | 0 | viscous_flux.cpp returns zero; no laminar viscous residual or wall-shear implementation. |
| Implicit And Transient Methods | 15 | 4.5 | time_stepper.cpp:115–165 implements local diagonal updates and CFL ramp; implicit_solver.cpp separate sweeps are no-op, transient BDF2 absent, no submitted convergence. |
| MPI | 10 | 1 | METIS graph call and MPI scaffolding exist, but non-root global meshes are empty, halo sends zeros and never unpacks; no distributed result. |
| Case Results And Validation | 10 | 0 | No submitted case package, field, force history or rank comparison. |
| Report, Visualization, Analysis | 5 | 0 | No report, plots or manifest submitted. |
| Extensibility | 5 | 3 | Modular mesh, gas and solver structure offers some extension points but many methods are no-op, 2-D assumptions pervasive and output disconnected. |

Pre-DQ score **24.5/100**, previously 27.

## Results and case outcomes

All eight independent case scores remain **0/5**: no contestant-delivered result package, force/field evidence or report was submitted. The single partial evaluator diagnostic does not change these scores. Weighted Code/CFD/Results scores are **2.7/1.46/0.0**.

## Disqualification assessment

Disqualified: **no**.

Every one of the 13 DQ flags was assessed. The missing results/report support zero completion and report/result credit, but do not alone establish fabrication, a false completion claim, a rank-count mismatch, misnamed figures, or copied solver code. There is no submitted metadata/report to make a method or completion claim contradictory to source. The no-external-copy trust policy was respected: no external similarity search was performed.

## Limitations

The bundled telemetry does not state the context windows for DeepSeek V4 Flash or DeepSeek V4 Pro; both are recorded unavailable. The two terminal sidecar hashes above preserve provenance, but remain outside the current index contract. No evaluator action repaired absent contestant deliverables.

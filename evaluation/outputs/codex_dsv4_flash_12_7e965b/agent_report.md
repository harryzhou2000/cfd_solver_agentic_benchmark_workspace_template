# Agent Evaluation Report — DSV4 Flash/12

Curated submission `97bdc69583d3f31441466cb7349d5632494e3897` on `codex/dsv4_flash/12` passes the history-preserving immutable audit. The final-tip cleanup removes 316 prohibited raw/generated paths from the index while retaining them physically as read-only evaluation evidence; the blobs remain reachable from preserved contestant ancestors.

## Evidence and method

The eight required final directories and `solver/report` were validated explicitly with `examiner/validate_outputs.py`; all nine targets returned `OK`. I inspected the committed C++/CMake/report sources, the workspace-local final raw data and manifests read-only, and the source/report claims. No evaluator rerun was performed; specifically, Re200 was not rerun.

The manually selected workspace-local Codex root is `019ffa41-6454-7503-82d8-5eb4dcf047e9`, spanning 2026-08-13T08:34:57Z to 2026-08-14T08:34:34Z with its seven spawned descendants. It is the only root and contains the benchmark objective, implementation, build/run/report work, and terminal answer `msg_ocx_f6e7ef715199487ea65e2fd4f1822e2e_1`. The exact extracted final answer is in `contestant_final_response.md` (SHA-256 `17a1ad6190cc2ba88aa2eead1cf9ba13df6acbe948d33bb791fbc81cf99ae5a6`). `run_identity.json` SHA-256 is `0eaa4e0aa4c2cd79d40d63a55e9be5ef645d29e8d2ee2414961de6e9c9acd821`. These Markdown/identity sidecars are unindexed under the current index contract and are not validated by `cfdeval check`.

## Rubric: 90/100

| Section | Score | Evidence |
|---|---:|---|
| Build, CLI, Output Contract | 9/10 | Build/CLI evidence and eight structurally valid packages; MPI missing-mesh error path remains weak. |
| Mesh And Geometry | 10/10 | CGNS mixed-cell/two-zone import and graph construction are implemented. |
| Residual And BC | 14/15 | Conservative residual, Roe/Rusanov, farfield/slip/no-slip source evidence. |
| Second Order | 9/10 | LSQ reconstruction, Venkatakrishnan and positivity fallback active; no formal order study. |
| Viscous Terms | 10/10 | Gradient/flux/wall-force evidence and readable wall outputs. |
| Implicit And Transient | 14/15 | LU-SGS/BDF2 and Re200 controls are evidenced; report presentation omits some required inner statistics. |
| MPI | 8/10 | METIS and neighbor halos are real; final rank-count differences need tighter reconciliation. |
| Case Results | 9/10 | Eight readable valid packages; M2 inviscid only reaches its disclosed 2.40-order plateau. |
| Report And Analysis | 3/5 | Extensive figures and manifest exist, but report/manifest accuracy claims have limitations. |
| Extensibility | 4/5 | Clean modular 2-D design, constrained by fixed-size state/dimensionality. |

Independent case scores: M0.15 inviscid 4.5, M0.80 inviscid 4.5, M2 inviscid 3.0, M0.15 Re5000 4.5, M0.80 Re5000 4.5, M2 Re5000 4.0, cylinder Re20 4.5, cylinder Re200 4.5.

## Disqualification

No DQ trigger is supported by direct evidence. Source implements METIS partitioning and neighbor exchange rather than a wrapper/geometric-only or replicated-state solver; the final packages are complete and validator-readable; methods claimed in the report are found in source. The 13 explicit false findings are in `agent_scores.json`.

## Limitations and verdict

This is a strong, complete workspace-evidence submission, not a clean Git-only raw-data submission: curation rules require raw outputs, logs, PDF, and manifest CSV to remain uncommitted at the result tip. The terminal answer claims a clean LaTeX build, but the evaluator did not reproduce it; report craftsmanship is scored from committed TeX/figures and cross-checks, not from that assertion. No source copying investigation was performed, consistent with the trust policy.

Verdict: **90/100, no disqualification**. Identity is `codex_dsv4_flash_12_7e965b`; raw evidence stays in the contestant workspace and was not copied into this snapshot.

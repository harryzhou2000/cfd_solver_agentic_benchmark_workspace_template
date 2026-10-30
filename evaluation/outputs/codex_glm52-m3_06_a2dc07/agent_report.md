# Agent Evaluation Report — GLM52-M3/06

Result branch `codex/glm52-m3/06` preserves contestant checkpoint `14db231df9f2b5ecb676a3a84a50c3b759d35626`; descendant curation tip `a063106730985373ab0dcf32c5247f79f391470f` passes the immutable audit. The tip removes 35 prohibited report artifacts from the index while retaining them on disk as read-only evidence. Earlier contestant commits still retain those blobs.

## Session accounting correction

Fork replay previously added 717,212 inherited tokens to descendant accounting. The selected tree is corrected from 325,794,414 to 325,077,202 owned tokens. With audited current price metadata, the stored estimate changes from $35.6875 to $101.8531. Evaluation evidence, scores, and DQ verdict are unchanged.

## Evidence and validation

I inspected the committed C++/CMake/report sources and workspace-local output evidence read-only. The official validator returned `OK` for all seven steady final packages, but correctly rejected `cylinder_m010_laminar_re200`: its metadata has `completed=false`. This is a substantive failed case, not an infrastructure error. Re200 was not rerun.

The selected workspace-local Codex root is `019fff60-e8b8-7990-8df2-f0784bde7d60`, with its seven descendants. It is the only root and spans the benchmark work; it ends blocked with an honest report that the AUSM+-up experiments could initiate but not sustain shedding. The exact final response sidecar SHA-256 is `f5bf0361a7cd37cb4cdd08bf6ae15b595878f6e31021ef97c52e9ff7de9d1c85`; `run_identity.json` SHA-256 is `89a4afdb6bc4ddb0a420ddf6aa9ff12e5ac3ea79e640b4065d8de7f3b3a85d70`. These sidecars are unindexed under the current contract and are not validated by `cfdeval check`.

## Rubric: 68/100

| Section | Score | Evidence |
|---|---:|---|
| Build, CLI, Output Contract | 7.5/10 | Clean build and 1/1 unit test pass; transient restart omits prior state/time and Re200 is correctly incomplete. |
| Mesh And Geometry | 9/10 | CGNS/mixed/multizone and graph code inspected. |
| Residual And BC | 11/15 | Finite-volume residual and BC code present, with simplified robustness. |
| Second Order | 5.5/10 | Fixed 0.3–0.5 reconstruction cap is not linear-exact; first-order M2 viscous is disclosed. |
| Viscous Terms | 7/10 | Formulation exists, but viscous force credibility is weak. |
| Implicit And Transient | 10/15 | BDF2 source exists; Re200 does not deliver a successful physical transient. |
| MPI | 8/10 | METIS/neighbor halo/rank sweep evidence. |
| Case Results | 4/10 | Seven packages pass structural checks; M015 inviscid is labeled converged despite a changing force trajectory, and Re200 is honestly failed. |
| Report And Analysis | 3/5 | Readable and candid about failure, but completion/traceability is partial. |
| Extensibility | 3/5 | Modular 2-D code, constrained scalar implicit architecture. |

Current case scores: M0.15 inviscid 1.5, M0.80 inviscid 3, M2 inviscid 2, M0.15 Re5000 1.5, M0.80 Re5000 2, M2 Re5000 2, cylinder Re20 2, cylinder Re200 0. Remaining case calibration is partial.

## Disqualification and limitations

Trigger 7 applies to the M015 inviscid case: its workspace metadata labels the 2500-step result converged although its force trajectory continues changing. The selected launch call and exact late-window evidence are documented in the scorecard. Re200 is separately and honestly labeled failed. Other DQ findings remain under review.

No evaluator rerun was performed. Raw results, generated PDF/logs, CSV manifests, and excluded figures were not copied into this manager snapshot. The final identity is `codex_glm52-m3_06_a2dc07`.

Verdict: **68/100, disqualified under trigger 7**. The solver has meaningful source/MPI work and seven structurally readable steady cases, but cannot receive Re200 case credit and has material physical-credibility limitations.

## Ongoing calibration

Case-floor and physical review are partial. In particular report.tex:132 supplies M2 inviscid Cd=0.0288 and near-zero Cl, contrary to the earlier missing-summary note, and contestant checkpoint 14db231d contains standalone case figures excluded by curation. Their visual adequacy and provenance must be checked before changing case scores. Manifest links the surviving workspace histories to reported final values; exact executable remains unbound.

Source review: clean a063106 build in restricted cfd-bench passed its one unit test. Restart reads rank-local current state only, omitting physical time and BDF history. Historical f262da0 explicitly implements BDF1 startup and BDF2. Reconstruction uses a persistent 0.3–0.5 cap on gradients; it remains nonzero but does not reproduce a smooth linear field exactly. The report discloses the cap and the M2 viscous first-order case, so DQ10 is not inferred from the cap alone. Re200 wake manifest incorrectly names forces.csv as source while plotting code reads field_final.vtu; this is a traceability defect, not proof of DQ8. Runtime executable and plot-data binding remain unresolved.

The report’s claim that both rank sweeps agree to about seven significant figures is inaccurate for Re20: submitted workspace step-200 Cd values are 18.9305, 18.9305, 18.9249 and 18.9227 for np1/2/4/8. The difference is small rather than order-one; metadata names the same revision but actual binaries and all controls are not independently bound. The M015 sweep reports Cd 0.284832 at all four ranks. These short tests do not establish production-scale rank independence. All six pages of the accepted compiled PDF (SHA256 b91fe2181a9e58658ba9ca6ea82b3507208849c5cf6813901f1731026248efe7) were independently rendered and checked; historical contestant PDF at 14db231d has SHA256 337be6a0c8b8079880a4a965cf6d805f25fbb88a761c4c339d48250a79490f2e.

# Agent Evaluation Report — codex_gpt56_01_cb349f

- Immutable submission: `af2fcc3ebfb2f58b8256f3b7604ba1b4373dfb4c` on `codex/gpt56/01`
- Provenance: reconstructed initial `codex/gpt56/init` / `92bc1c342d984fed987772948ef7d821dc5166bf`; post-run environment snapshot
- Result identity: `codex_gpt56_01_cb349f` (`run_identity.json` SHA-256 `223aeee53f7005b41451ae2bb37be17db4e3b7d60267c6b98f1a1b7e86c84680`)

## Session selection

Project Codex root `019fb9e3-ba6e-7e40-99d3-84d723942dc8` was selected, including its 21 recorded descendants. Its first persisted run event is `2026-07-31T20:37:34.058000+00:00` and terminal root final response is `2026-08-01T02:17:09.958Z`; execution date is therefore `2026-07-31`. The project SQLite records the host workspace while the root extraction used its container cwd `/workspace`. Earlier roots `019fb9cd`, `019fb9d5`, and `019fb9df` are excluded as setup/audit attempts. Root rollout SHA-256: `cb2c2ab87b31ace33a14f5890c1eec83585ad35d3f53b05448741bec48b85f75`. All telemetry sidecars were regenerated from the explicit selected root and its descendants; excluded setup/audit roots are absent from the corrected timeline and measurements.

The final response was extracted exactly to `contestant_final_response.md` from message `msg_08abbf587d5f95fa016a6d5720778c819e93619edaaeed5484`, one ordered text part, no redactions; SHA-256 is `40ef89590dc771cd627cb5b423cac16232fe50a80799a956a672a772921c1fa4`. `run_identity.json` SHA-256 is `223aeee53f7005b41451ae2bb37be17db4e3b7d60267c6b98f1a1b7e86c84680`. Both are unindexed sidecars, so `cfdeval check` does not validate them.

### Session accounting correction

Forked Codex rollouts replay parent history. Excluding that inherited prefix corrects the selected tree from 946,579,960 to 212,765,117 owned tokens, removing 733,814,843 replayed tokens. The current-price estimate changes from $427.5828 to $133.4586. Evaluation evidence, scores, and DQ verdict are unchanged.

## Evidence and validation

The explicit command `validate_outputs.py --report <historical report> <all eight historical final directories>` returned `OK` for every required case and the report (rc 0). The read-only historical artifacts show seven `converged` steady cases and Re200 `statistically_periodic` at `t=300`, with 0.9971 inner-target attainment. The report statistics give Re20 tail mean Cd 2.04689; Re200 mean Cd 1.13563, lift amplitude 0.32513 and St 0.16667.

Submitted source inspection found `METIS_PartGraphKway`, neighbor-scoped `MPI_Isend/Irecv`, `MPI_Allreduce` for forces/residuals/Krylov operations, weighted least-squares reconstruction, active Barth-Jespersen limits, density/pressure safeguards, corrected viscous face gradients, and frozen-history BDF2. Historical np1/2/4/8 comparisons for NACA M0.15 inviscid and cylinder Re20 show terminal force agreement at about 1e-9 to 1e-10.

All 49 curated PNGs are referenced by committed `solver/report/report.tex`; the historical manifest maps each to a readable CSV/VTU source. Direct LaTeX compilation from `solver/report/` succeeded (48 pages). `solver/tools/build_report.sh` fails from `solver/`, however, because it invokes `report/report.tex` while that source's `figures/` path is relative to the report directory. The immutable submission was not modified.

## Score and DQ verdict

Score: **97/100**. The three-point deduction is two points for independent clean-build/recovery reproducibility limits and one point for the report-wrapper working-directory defect. No DQ trigger was found from direct internal evidence. Originality/copying was not externally investigated under the trust policy, and is retained as an explicit limitation rather than a positive assertion.

## Limitations

This is an operator-authorized legacy recovery. The 1.1 GB results archive, raw CSV/JSON/logs/fields, manifest/statistics, generated PDF, and original dirty submodule remain outside the result commit. They were inspected read-only for evaluation; no Re200 run was launched. `run_identity.json` and the final-response Markdown sidecar are outside the current index schema and are not covered by `cfdeval check`.

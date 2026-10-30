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

## Calibration in progress

Re200 case score 2→4.5: the submitted force plot visibly resolves both late lift and drag oscillations, contrary to the previous missing-plot assertion. The historical t180–300 force record independently gives mean Cd 1.135626, lift range −0.32436..0.32589 and mean period 6.085 from 20 upward crossings, consistent with the reported St 0.16667. Residual and wake figures were also reviewed. Exact source paths, hashes and executable-provenance limit are recorded in the case note. Subsequent calibration reviewed the remaining cases, rubric and DQ criteria; executable and individual figure bindings remain qualified.

The seven steady force, residual and Mach plots and historical final windows were independently inspected. M015 inviscid Cd drops 0.0007517→0.0005541 over its last 200 samples: the absolute change is small but its relative size limits the result, so that case scores 4 rather than 5. M080 inviscid terminal Cd span is about 1.3% of final and scores 4.5. The other five steady scores remain 5; case notes record individual values, hashes and limitations. This is not by itself a DQ finding.

Source calibration finds a force-split discrepancy: report.tex:195 claims integrated tangent-projected viscous traction, whereas physics.cpp:351–356 constructs full traction and solver.cpp:618–628 integrates both components. Surface cf separately projects tangentially at solver.cpp:1094–1102. The normal contribution is unquantified. Viscous rubric 10→8.5, report 4→3.5 and case results 10→9.5 for the independent low-drag terminal drift, giving provisional 94/100. Following the documented sonnet46-02 and dsv4-03 precedent, the force output uses a different code path and does not automatically trigger DQ6. Clean source built with bundled Boost explicitly included; seven CTests passed and both MPI tests passed after container-only MPI root/oversubscription settings. No CFD rerun.

Restart source writes only global cell IDs and the current state (output.cpp:462–476). The transient loop initializes both BDF history states from that state and a fresh physical-time horizon (solver.cpp:1462–1477), so this supports state initialization rather than exact transient continuation. Build/CLI rubric 8→7.5; provisional total 94/100. Nine CTests passed across two invocations after adding the bundled Boost include and container MPI root/oversubscription settings; no Re200 solve was run.

All 48 accepted compiled PDF pages (SHA256 d273c00b0ed72c59e9acce52b3ae0b6c9d30d9e1228c5c4a793a410e6563f5a6) were rendered and visually reviewed. Historical rank-sweep endpoints agree closely: Re20 np1/2/4/8 Cd 2.04673583927/2.04673583947/2.04673583932/2.04673583928 at step4186, and M015 inviscid Cd 0.0005541454/0.0005541452/0.0005541454/0.0005541437 at step2785. Metadata reports revision461385a; exact executable remains unbound. DQ flags are individually qualified; no DQ is established.

The available evidence review is complete: all 21 curated solver/src files match corresponding files at historical submodule revision 461385a3d4472c0fa2c2c9f46bced39937ea4819 named by the production metadata. This does not independently identify the historical executable or prove every figure-to-data binding. Final rubric is 94/100 with no DQ established, subject to those limitations.

# Agent Evaluation Report — claude_generic_opus-01_9a712c

- Submission: `claude/generic/opus-01` at `8dae7ba6b828c8b72dfa9e55e407f6c4e8098303`.
- Immutable checkpoint: `0406abe520ee85b4383912a1e3ef2071885fe0f4`; pre-run initial: `claude/generic/init` at `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82`.
- Explicit eight-case plus report validator: pass.
- Run identity SHA-256: `48316000479d83adaeb6d43d80c2e6dc23fb43f3d2b8014f2671ddb14c659913`.
- Final-response SHA-256: `2c1c74f44a27d92cb7666f5723b1d2673c2ff50e71656921dec1504944c9abf5`.
  These are unindexed sidecars; `cfdeval check` does not validate them.

## Methodology and calibrated assessment

Reviewed immutable submission `8dae7ba6b828c8b72dfa9e55e407f6c4e8098303`, original case and rank-study outputs, report source and figures, and terminal response. The explicit validator passed all eight packages and the report. A clean CMake build and kernel ctest passed in a network-disabled resource-limited scratch container with mounted externals. No case was rerun; in particular Re200 was not rerun. Structural validation does not establish physical accuracy.

The prior 97/100 rubric is recalibrated to **93.5/100**, with criterion-specific evidence in the scorecard. Code, CFD and Results review overalls are recomputed from their individual points. In canonical task order, independent case scores change from 5, 5, 5, 5, 5, 5, 5, 5 to **3, 4, 4, 4, 4, 3, 4, 2**. These case scores are independent of the rubric and review-area scores. The last Re200 change was already checkpointed separately.

M2 laminar original `solver/results/naca0012_m200_laminar_re5000/forces.csv` has 12,221 rows; the last 1,223 increase from Cd 0.1117185 to 0.1146643, about 2.57% of final Cd. That is a longer window than the solver's 500-step stationarity criterion and does not by itself prove false metadata. Steady laminar force plots compress settled Cd due to startup extremes but show force-history evolution and separately scaled lift; the general steady-case floor is applied consistently with the prior Opus-04 review. The Re200-specific floor explicitly requires visible late lift and drag, and its committed plots do not show resolved late Cd.

The result retains **DQ=false**: the submitted methods exist and the original rank comparisons agree closely. `solver/src/io/Writers.cpp:381–409` stores only the current state, while `solver/src/solver/Driver.cpp:588,618–638` sets the previous time level equal to current and uses BDF2 immediately after transient restart. This limits the first resumed step, not the delivered fresh Re200 calculation. The report's cross-rank restart demonstration is for steady Re20. The quantitative report defects below are not by themselves evidence of a missing algorithm or fabricated solver data. Trigger-by-trigger evidence and limits are in the scorecard; no full external authorship or pixel-level figure audit is claimed.

## Session selection

Selected Claude root `3a8e926a-3e32-45e8-a8dd-893091c1e00f`, 2026-08-25T12:24:12.933Z–17:40:38.570Z UTC, because it contains the benchmark prompt, continuous build/run/report work, five persisted subagents, and terminal `end_turn` response at source line 4739. The similar root `08bb7dba-...` has no persisted subagent tree; two PONG probes were excluded.

## Limitations

The canonical workspace external symlink is dangling. The isolated evaluator build and kernel test passed using mounted externals, as recorded in the calibration ledger. Raw outputs and generated report PDF were removed only at the curated result tip but retained read-only in the workspace for validation. The result identity and final-response Markdown are unindexed sidecars as noted above.


## Partial Re200 correction — 2026-10-29

The independent Re200 case score changes from 5 to 2 under the required late-time lift/drag plotting floor. Immutable `solver/report/report.tex:1046,1103–1104` references a full force plot and a separate late lift/spectrum plot. Visual review confirms the full plot is dominated by the startup drag spike and does not resolve late drag oscillations; the separate plot contains only lift. No alternate committed Re200 force/drag plot was found. This is a case-result deliverable cap, not a solver-functionality verdict. The report table :664–679 calls the Re200 force row post-transient means; the original force history for t>=180 has means Cd=1.2287960, pressure=0.9919030, viscous=0.2368931, but the displayed pressure 1.0020 and viscous 0.2381 match the final row rather than those means. This is a traceability issue, not evidence that the underlying result was fabricated. The source, rubric and DQ were subsequently reviewed as described above. No Re200 rerun.

## Additional quantitative report defect

Immutable `solver/report/report.tex:1232–1237` says excluded normal viscous drag is several orders of magnitude smaller than pressure drag in every case. Original `solver/results/naca0012_m200_laminar_re5000/metadata.json` instead records normal viscous drag 0.01886847373593819 and the report lists pressure drag about 0.0771, a ratio near 24.5%. `solver/src/solver/Forces.cpp:32–44` separately projects tangential traction, so the implementation exists; the report's quantitative assessment is wrong.

# Agent Evaluation Report — claude_generic_opus-01_9a712c

- Submission: `claude/generic/opus-01` at `8dae7ba6b828c8b72dfa9e55e407f6c4e8098303`.
- Immutable checkpoint: `0406abe520ee85b4383912a1e3ef2071885fe0f4`; pre-run initial: `claude/generic/init` at `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82`.
- Explicit eight-case plus report validator: pass.
- Run identity SHA-256: `48316000479d83adaeb6d43d80c2e6dc23fb43f3d2b8014f2671ddb14c659913`.
- Final-response SHA-256: `2c1c74f44a27d92cb7666f5723b1d2673c2ff50e71656921dec1504944c9abf5`.
  These are unindexed sidecars; `cfdeval check` does not validate them.

## Methodology

Read the immutable submitted source, the contestant report and exact terminal response, and all eight original result packages. The explicit validator passed all eight packages and the report. Source inspection confirmed CGNS mesh import, METIS graph partitioning, neighbor MPI exchange, finite-volume residual/BC paths, reconstruction/positivity controls, implicit march, and BDF2 dual-time code. No evaluator rerun was performed; Re200 was not rerun.

## Scores and verdict

The prior rubric was 97/100; the rubric and review-area scores have not yet been recalibrated. Seven independent case scores remain 5/5 pending review, while the Re200 score is now 2/5 for the plotting limitation documented below. The submitted Re200 package reaches t=300 with nonzero lift oscillation, 7–157 reported inner iterations, zero target misses, and final ratio 9.46e-4.

No DQ trigger was found. The report claims map to source: METIS_PartGraphKway, MPI_Isend/Irecv neighbor exchange, implicit LU-SGS-style relaxation, and BDF2 frozen-history outer/inner loops. The figure/result checks and statuses are consistent.

## Session selection

Selected Claude root `3a8e926a-3e32-45e8-a8dd-893091c1e00f`, 2026-08-25T12:24:12.933Z–17:40:38.570Z UTC, because it contains the benchmark prompt, continuous build/run/report work, five persisted subagents, and terminal `end_turn` response at source line 4739. The similar root `08bb7dba-...` has no persisted subagent tree; two PONG probes were excluded.

## Limitations

The canonical workspace external symlink is dangling. A later isolated evaluator build and kernel test passed using mounted externals, as recorded in the calibration ledger. Raw outputs and generated report PDF were removed only at the curated result tip but retained read-only in the workspace for validation. The result identity and final-response Markdown are unindexed sidecars as noted above.


## Partial Re200 correction — 2026-10-29

The independent Re200 case score changes from 5 to 2 under the required late-time lift/drag plotting floor. Immutable `solver/report/report.tex:1046,1103–1104` references a full force plot and a separate late lift/spectrum plot. Visual review confirms the full plot is dominated by the startup drag spike and does not resolve late drag oscillations; the separate plot contains only lift. No alternate committed Re200 force/drag plot was found. This is a case-result deliverable cap, not a solver-functionality verdict. The report table :664–679 calls the Re200 force row post-transient means; the original force history for t>=180 has means Cd=1.2287960, pressure=0.9919030, viscous=0.2368931, but the displayed pressure 1.0020 and viscous 0.2381 match the final row rather than those means. This is a traceability issue, not evidence that the underlying result was fabricated. Remaining source/rubric and DQ calibration is still partial; prior rubric and review-area scores have not been reverified. No Re200 rerun.

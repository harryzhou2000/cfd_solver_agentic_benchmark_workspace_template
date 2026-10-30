# Evaluation — claude_generic_sonnet-01_96c411

Submission `claude/generic/sonnet-01` at `64bbe1075e1413091acf116733e27241441ed6e2`; checkpoint `7f6a2ad77bc9acb93484a6bf099dfc2ae6887342`.

## Verdict

**71/100; DQ=false.** Two steady cases reach residual targets, but force outputs are unreliable; the other five steady cases and Re200 are explicitly failed, and the original output validator fails final metadata.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: 3e8776452c691a5a3a608f1170f80b5341d3b86a2ff130923ccffd46eaa74b63. contestant_final_response.md SHA-256: 0e36955b931a6f839ce4ae97cc18302bbe2639e23842c75fa7ebb63032db3da3. These unindexed sidecars are not validated by cfdeval check.

## Partial report provenance correction, 2026-10-30

The contestant delivered the 36-page sibling PDF SHA-256 `fe1abdd4fadde590638e9d186293f0898df92980afc1fad1181cc6a0503fca9c`, previously accepted and preserved in the workspace and manager history `9986925f`. Visual review found explicit missing-asset TODO boxes for Re200 wake velocity and vorticity on p33; under current acceptance criteria it is unsuitable as an approved snapshot PDF, so the PDF sidecar records absence with reason. Its readable content remains contestant evidence: p31 has partial residual and force plots, while original status t=3.03 and force CSV t=2.86 demonstrate the run is incomplete. This was a partial checkpoint; the full review below supersedes it.

Case-by-case review retains 1,1,1,2,1,2,1,0 in task order. The report has actual steady plots, but five steady cases fail residual targets and the two nominally converged laminar cases have materially unreliable force outputs. Re200 covers only approximately t=3 of 300 with 303 measured misses. Original force tails and the pressure/viscous normalization defect are detailed in the case notes. This was a partial checkpoint; the full review below supersedes it.

## Full calibration and disqualification assessment

Rubric 74→71/100; all ten sections and 27 review points contain criterion-specific source and output evidence. Eight case scores remain 1,1,1,2,1,2,1,0. The two nominally residual-converged cases have unreliable force values; five steady cases failed and Re200 reached only t=3.03/300 with 303 measured inner misses. Report.tex:940–960 discloses the force defect, while Residual.cpp:295–305,341–349 provides specific sign and component-normalization evidence.

DQ remains false: submitted failed metadata is honest, METIS and rank-local exchange are present, full mesh exists only on rank 0 before solving, and reconstruction is active. The code would mark a hypothetical reached transient horizon periodic without checking its target-miss fraction, but this submitted run was marked failed; that defect is scored without claiming an actual false success. Figure p31 corresponds to partial source history though its post-transient label overstates it. No affirmative copied-core or mismatched-figure evidence found in this review; no exhaustive external authorship or pixel audit. The immutable source clean build and non-CFD gas/geometry/writer unit target passed in a network-disabled restricted container. Original output validator fails on incomplete cases; both manager record gates pass. No CFD or unsteady rerun.

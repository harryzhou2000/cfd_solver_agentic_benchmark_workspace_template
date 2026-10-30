# Evaluation — claude_generic_opus-11_fe7eba

Submission `claude/generic/opus-11` at `9965be84b59114b4fb573a9253f9e14e24edfa94`; checkpoint `1ac9e50fb6ef2b55817b4ae85376ba2f2946214f`.

## Verdict

**92/100; DQ=false.** Eight packages and report pass the explicit validator; submitted report/manifest provides credible steady and Re200 evidence.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: b74bf0fbde9bfd9bc74298edd069cbf343d1a99e6a2e82552dd57c55a817ef92. contestant_final_response.md SHA-256: bf85a0d68561a32e82daaa1d4aaecabc0d04b923a24de162bd9df86e4d2f0853. These unindexed sidecars are not validated by cfdeval check.

## Partial recalibration, 2026-10-30

Accepted contestant PDF SHA-256 `95a9bf9d109d4c2e2aad1731a8492987fbde3b0a2fd3bd635c9c1d70b3c190ba` matches the indexed bytes; sibling TeX hashes to immutable blob `14d9f2de273930cd88df921c79fe9313f7476603`. PDF p3 reports Re200 mean Cd 0.919, lift amplitude 0.388 and St 0.190; p8 shows wake and full force history. Original t≥150 data gives mean Cd 0.918708, lift range −0.387379–0.387772 and crossing frequency 0.189919. However the full-history drag axis is dominated by startup ~31 and late drag is unreadable, and the PDF has no Re200 residual history. The contestant explicitly delivered 42 standalone figures and a manifest in its final response, and the case-specific Re200 residual plot exists and is readable; the PDF-only assessment omitted this deliverable. Case score remains 2 under the delivered-evidence floor. Missing figure source blobs separately limit report reproducibility. Other case scores, rubric and DQ remain under review; the earlier verdict is provisional. No CFD rerun.

The selected final response at 2026-08-28 09:03:41 UTC explicitly delivers 42 case figures and a figure manifest. The three inviscid case plot sets (`report/figures/<case>_{residuals,forces,mach}.png`) predate it by about five minutes and are mapped to residuals.csv, forces.csv and field_final.vtu by `figure_manifest.csv`; each was visually checked against case IDs and original histories. Scores for M0.15, M0.8 and M2 inviscid each rise 2→3; force-tail and residual limits are stated in case notes. The standalone plots are contestant artifacts, not evaluator redraws; their exclusion from the curated commit remains a separate reproducibility limitation. Other case scores and overall review remain partial.

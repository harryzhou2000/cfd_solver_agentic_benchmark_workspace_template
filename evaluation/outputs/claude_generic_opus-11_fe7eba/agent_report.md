# Evaluation — claude_generic_opus-11_fe7eba

Submission `claude/generic/opus-11` at `9965be84b59114b4fb573a9253f9e14e24edfa94`; checkpoint `1ac9e50fb6ef2b55817b4ae85376ba2f2946214f`.

## Verdict

**84/100; DQ=false.** The clean immutable build and structural validation pass; individual case scores and force-history limitations are recorded below. Re20 drag is substantially underpredicted, the restart option cold-starts, and several delivered force plots do not resolve their late histories.

## Evidence

- Read selected workspace-local terminal response, committed source/report, contestant result packages, and run manifest.
- Ran the examiner validator explicitly against all eight expected final directories and the report; its outcome is recorded above.
- Visually reviewed the frozen main report PDF. No evaluator rerun was performed; Re200 was never rerun.

## Limitations

Curation is index-only. Raw outputs/logs/restarts remain workspace evidence but are absent from the immutable tip. `run_identity.json` and `contestant_final_response.md` are unindexed sidecars.

run_identity.json SHA-256: b74bf0fbde9bfd9bc74298edd069cbf343d1a99e6a2e82552dd57c55a817ef92. contestant_final_response.md SHA-256: bf85a0d68561a32e82daaa1d4aaecabc0d04b923a24de162bd9df86e4d2f0853. These unindexed sidecars are not validated by cfdeval check.

## Partial recalibration, 2026-10-30

Accepted contestant PDF SHA-256 `95a9bf9d109d4c2e2aad1731a8492987fbde3b0a2fd3bd635c9c1d70b3c190ba` matches the indexed bytes; sibling TeX hashes to immutable blob `14d9f2de273930cd88df921c79fe9313f7476603`. PDF p3 reports Re200 mean Cd 0.919, lift amplitude 0.388 and St 0.190; p8 shows wake and full force history. Original t≥150 data gives mean Cd 0.918708, lift range −0.387379–0.387772 and crossing frequency 0.189919. However the full-history drag axis is dominated by startup ~31 and late drag is unreadable, and the PDF has no Re200 residual history. The contestant explicitly delivered 42 standalone figures and a manifest in its final response, and the case-specific Re200 residual plot exists and is readable; the PDF-only assessment omitted this deliverable. Case score remains 2 under the delivered-evidence floor. Missing figure source blobs separately limit report reproducibility. This was a partial checkpoint; the final review below supersedes it. No CFD rerun.

The selected final response at 2026-08-28 09:03:41 UTC explicitly delivers 42 case figures and a figure manifest. The three inviscid case plot sets (`report/figures/<case>_{residuals,forces,mach}.png`) predate it by about five minutes and are mapped to residuals.csv, forces.csv and field_final.vtu by `figure_manifest.csv`; each was visually checked against case IDs and original histories. Scores for M0.15, M0.8 and M2 inviscid each rise 2→3; force-tail and residual limits are stated in case notes. The standalone plots are contestant artifacts, not evaluator redraws; their exclusion from the curated commit remains a separate reproducibility limitation. This was a partial checkpoint; the final review below supersedes it.

For the three laminar airfoils and Re20, separately delivered residual and field plots are available, but their full force-history plots have startup peaks approximately 330–360 that compress final forces near the axis; each case remains capped at 2. Their original force tails and the report’s disclosed Re20 underprediction are documented per case. This was a partial checkpoint; the final review below supersedes it.

## Full calibration and disqualification assessment

The ten rubric sections and all 27 review points now carry criterion-specific evidence; total 92→84/100. The source build succeeded from immutable 9965be84 in a network-disabled cfd-bench container (two CPUs, 3 GiB), with image-provided CGNS/HDF5/METIS. The eight case scores in task order are 2,2,2,2,2,2,2,2 → 3,3,3,2,2,2,2,2. Submitted Re200 force history supports St≈0.189919 and actual measured inner target statistics; Re20 Cd≈0.878 is much lower than the disclosed ≈2 reference.

No affirmative disqualification is established. METIS partition and neighbor halos exist, full mesh scope ends before solve, production reconstruction is active, no-slip surface output explicitly gives wall values, and TimeLoop counts actual inner target outcomes. The M2 inviscid best-residual plateau is disclosed with a stable final force tail. The integrated viscous force uses full traction while the report calls it tangential; the implemented output is a different split, not a missing claimed solver algorithm, and the viscous/report scores reflect the defect. Figures checked against case data and the contestant manifest do not establish a mismatched plot; this is not an exhaustive pixel or external-authorship audit. No solver rerun was performed.

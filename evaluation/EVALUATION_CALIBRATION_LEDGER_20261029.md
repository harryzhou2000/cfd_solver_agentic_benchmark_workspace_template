# Evaluation calibration ledger — 2026-10-29

This is a work-in-progress evidence ledger, not a completion claim. Passing structural gates or automated candidate triage do not establish calibrated scores. Reviewed means substantive manual review is complete across the applicable rubric, case results, scorecards and DQ evidence; corrected means a substantiated change passed both current gates. Partial items remain unreviewed for overall calibration. No unsteady or Re200 case has been rerun in this review.

## Historical 30-minute review checkpoint

The following paragraph describes the earlier checkpoint, not the current
state. All three new submissions have since been recorded and committed as
shown in the next table; the 03 DQ decision was reviewed and changed to false.

The three new snapshot directories remain untracked working drafts. All three
have provisional per-case evidence; the 01 and 02 rubric totals and DQ verdicts
remain unfilled. The 03 draft has a provisional 78/100 and DQ=true and passes
`cfdeval check` and `cfdeval check-complete`, but its DQ interpretation has not
yet been compared with prior evaluations of the same force-split defect. Its
record must not be treated as a final adjudication until that comparison and
review are completed. The factual issue is that the report claims tangential
viscous force while the integrated-force source path uses full stress traction;
the possible magnitude has not been verified across cases. The review is
incomplete and no manager snapshot for these three has been committed.

## New submissions — reviewed and recorded

| Run ID | Status | Evidence and remaining work |
|---|---|---|
| `codex_generic_dsv4.1f-1017-01_4f9379` | reviewed and recorded | 73.5/100, DQ=false; contestant `f156e96bdde30de9002d837731bc2fad337014a8`, manager snapshot `fc50e537`. Both gates passed. High steady drag, full-stress force split and noisy Re200 evidence are documented; no Re200 rerun. |
| `codex_generic_dsv4.1f-1017-02_df914f` | reviewed and recorded | 78/100, DQ=true under trigger 7 for M0.15 steady numerical limit cycle marked complete/statistically periodic; contestant `751d2ca9617f481a26cb7ff69c0844984a98d3ef`, manager snapshot `a9338172`. Both gates passed. Re200 forcing is disclosed and unforced shedding is not established; Re200 was not rerun. |
| `codex_generic_dsv4.1f-1017-03_dbc1f8` | reviewed and recorded | 78/100, DQ=false; contestant commit `89dee562643ce2b47f43e4a2b19989307f5b7858`, manager snapshot `f7bf749e`. Both integrity and completion gates pass. The report/source wall-traction mismatch is charged to viscous/report criteria, with the DQ 6 precedent distinction and unquantified normal contribution documented in the snapshot. Re200 was not rerun. |

Result branches preserve contestant history, which contains prohibited generated files; curation removes those files from the tip index only. Raw workspace evidence is retained. All three new snapshots have now been reviewed, recorded and committed separately; the historic catalog table below remains explicitly partial.

## Historical calibration finding and subsequent correction

The earlier `claude_generic_opus-04_616ae8` record gave 99/100 and case scores 2, while its report said 94/100 and each case 5. Its notes asserted that committed and referenced plots were missing, but at submission `c2fefc174dffcb7e5119e7ab54f971479d5dfe25`, `solver/report/report.tex:875` inputs committed `solver/report/generated/case_sections.tex`, which references figures that are present. After case-specific and full rubric review, commits `48281ae3` and `eebd7dea` corrected the case scores and calibrated the rubric to 92.5/100, DQ=false; both gates pass. The report contains the detailed evidence and limitations. A dependency-aware candidate screen additionally flagged `codex_gpt56_04_1e36f2`, `codex_kimik3_04_b353a7`, `oc-goal_kimik3-dsv4_02_0ceb76`, `codex_dsv4_flash_03_294b64`, `codex_kimik3_07_f42559`, `codex_kimik3_05_382d41`, `codex_dsv4_flash_14_80096b`, `oc-goal_kimik3-dsv4_01_940dcb`, and `codex_gpt56_12_e9972d`; these are leads, not findings or justified score changes.

## Additional leads awaiting substantive review

- Historical `claude_generic_opus-02_6fc51c` lead (fully reviewed in
  `5dd2ad5a`): the earlier Re200 case note said there was no
  committed force summary or shedding statistic, but submission `6ed4241`
  `report/report.tex:333–338` reports mean Cd, lift amplitude, and St, and
  `:586` summarizes CL/CD. A readable late-time force plot and numerical
  credibility remain separate requirements. The report says 86/100 while the
  current scorecard says 82/100; its other overalls also differ.
  Partial follow-up review: the clean immutable CMake build succeeded in a
  network-disabled container with `JSON_ROOT` supplied, and the explicit
  eight-case/report validator passed. The evaluator visually screened all 12
  report-PDF pages and found no Re200 force-history figure; the case remains
  capped pending full adjudication. The report discloses first-order steady
  reconstruction (`report/report.tex:91–93,561–564`), and
  `solver/src/solver.cpp:768–775` zeros the gradients and limiters, while
  `solver/src/io.cpp:480–482` writes `spatial_order_claimed: 2`. The restart
  output is a case-name/step placeholder (`solver/src/main.cpp:195–198`), and
  the evaluator found that the CLI does not parse `--restart`. These findings
  were assessed in the completed record; no unsteady case was rerun.
- A partial automated scan of explicit Code/CFD/Results overalls in reports
  found 22 mismatched mentions among 32 mentions checked. It did not inspect
  every report or determine the correct score. At screening time, `opus-05` reported
  4.6/4.6/4.7 versus scorecard 5.0/4.92/5.0. Reconcile each when reviewing
  its substantive evidence; do not mechanically substitute one for the other.

## Existing catalog status

| Run ID | Review status |
|---|---|
| `claude_generic_opus-01_9a712c` | fully reviewed — 93.5/100, DQ=false; both gates pass |
| `claude_generic_opus-02_6fc51c` | fully reviewed — 65.5/100, mandatory placeholder-result rejection; both gates pass |
| `claude_generic_opus-03_68c1ac` | fully reviewed — 58.5/100, DQ=true; both gates pass |
| `claude_generic_opus-04_616ae8` | fully reviewed — 92.5/100, DQ=false; both gates pass |
| `claude_generic_opus-05_21cf4d` | fully reviewed — 74.5/100, DQ=true; both gates pass |
| `claude_generic_opus-06_40b02c` | fully reviewed — 93/100, DQ=false; both gates pass |
| `claude_generic_opus-07_c08c67` | fully reviewed — 61/100, DQ=true; both gates pass |
| `claude_generic_opus-08_853afe` | fully reviewed — 50.5/100, DQ=true; both gates pass |
| `claude_generic_opus-09_9cb2fc` | fully reviewed — 65.5/100, DQ=true; both gates pass |
| `claude_generic_opus-10_f6b586` | fully reviewed — 87/100, DQ=true; both gates pass |
| `claude_generic_opus-11_fe7eba` | fully reviewed: 84/100, DQ false; eight case scores 3,3,3,2,2,2,2,2 |
| `claude_generic_sonnet-01_96c411` | fully reviewed: 71/100, DQ false; report PDF unsuitable for snapshot; both gates pass |
| `codex_dsv4_flash_01_0c1996` | partial: 48/100, DQ 6/7/10 and TASK full mesh; two historical raw np1 packages unavailable |
| `codex_dsv4_flash_02_3447d3` | partial: Re200 and DQ7 independently reviewed; other cases/rubric/review points pending |
| `codex_dsv4_flash_03_294b64` | unreviewed |
| `codex_dsv4_flash_04_755869` | unreviewed |
| `codex_dsv4_flash_06_5b6b08` | unreviewed |
| `codex_dsv4_flash_07_aeae21` | unreviewed |
| `codex_dsv4_flash_08_efba3c` | unreviewed |
| `codex_dsv4_flash_09_526316` | unreviewed |
| `codex_dsv4_flash_10_6468ba` | unreviewed |
| `codex_dsv4_flash_11_6e6ac8` | unreviewed |
| `codex_dsv4_flash_12_7e965b` | unreviewed |
| `codex_dsv4_flash_14_80096b` | unreviewed |
| `codex_generic_02_ed5ee6` | unreviewed |
| `codex_generic_03_5f2165` | unreviewed |
| `codex_generic_04_16009f` | unreviewed |
| `codex_generic_05_2599b1` | unreviewed |
| `codex_generic_glm53-01_99a969` | unreviewed |
| `codex_generic_glm53-02_ff20c7` | unreviewed |
| `codex_generic_glm53-03_d469fb` | unreviewed |
| `codex_generic_glm53-04_adde03` | unreviewed |
| `codex_generic_opus46-01_3a72ea` | unreviewed |
| `codex_generic_opus46-02_d8fed8` | unreviewed |
| `codex_generic_opus5-01_83492a` | unreviewed |
| `codex_generic_opus5-02_cac77b` | unreviewed |
| `codex_generic_sonnet46-01_ac6940` | unreviewed |
| `codex_generic_sonnet46-02_dac37b` | unreviewed |
| `codex_generic_sonnet5-01_540ddf` | unreviewed |
| `codex_generic_sonnet5-02_6c0ac0` | unreviewed |
| `codex_generic_vllm-qwen38-01_2045e0` | unreviewed |
| `codex_generic_vllm-qwen38-02_d47362` | unreviewed |
| `codex_glm52-m3_01_919b67` | unreviewed |
| `codex_glm52-m3_02_ca46e4` | unreviewed |
| `codex_glm52-m3_03_674665` | unreviewed |
| `codex_glm52-m3_04_32f2d0` | unreviewed |
| `codex_glm52-m3_05_f094c1` | unreviewed |
| `codex_glm52-m3_06_a2dc07` | unreviewed |
| `codex_glm52_01_1307c9` | partial — DQ mapping corrected, full rubric calibration pending |
| `codex_glm52_02_06cf18` | unreviewed |
| `codex_glm52_03_e7d38e` | unreviewed |
| `codex_gpt56_01_cb349f` | unreviewed |
| `codex_gpt56_02_38616e` | unreviewed |
| `codex_gpt56_03_93b257` | unreviewed |
| `codex_gpt56_04_1e36f2` | partial — see finding below |
| `codex_gpt56_05_7bee7c` | unreviewed |
| `codex_gpt56_06_97ae53` | unreviewed |
| `codex_gpt56_07_b7c2c8` | partial — DQ evidence/ID corrected; full rubric calibration pending |
| `codex_gpt56_08_715d70` | unreviewed |
| `codex_gpt56_09_d5760c` | unreviewed |
| `codex_gpt56_10_797ac3` | unreviewed |
| `codex_gpt56_11_14a0ed` | unreviewed |
| `codex_gpt56_12_e9972d` | unreviewed |
| `codex_kimik3_01_09309d` | unreviewed |
| `codex_kimik3_03_09eb62` | unreviewed |
| `codex_kimik3_04_b353a7` | unreviewed |
| `codex_kimik3_05_382d41` | unreviewed |
| `codex_kimik3_06_8208e5` | unreviewed |
| `codex_kimik3_07_f42559` | unreviewed |
| `codex_kimik3_08_75b3dc` | unreviewed |
| `codex_vllm-qwen38_03_da027b` | unreviewed |
| `oc-goal_gpt56-dsv4_01_f83d02` | unreviewed |
| `oc-goal_kimik3-dsv4_01_940dcb` | unreviewed |
| `oc-goal_kimik3-dsv4_02_0ceb76` | unreviewed |
| `oc-goal_kimik3-dsv4_03_eb4a9a` | unreviewed |
| `oc-goal_kimik3-dsv4_04_60cc7d` | unreviewed |
| `oc-goal_kimik3-dsv4_05_8d943e` | unreviewed |
| `omo_slim_dsv4_01_12c8d9` | unreviewed |
| `omo_slim_dsv4_02_b52628` | unreviewed |
| `omo_slim_dsv4_03_2fbbe7` | unreviewed |
| `omo_slim_dsv4_04_d9ebd4` | unreviewed |
| `omo_slim_dsv4_05_7a7141` | unreviewed |
| `omo_slim_dsv4_06_d8d434` | unreviewed |
| `omo_slim_dsv4_07_770c37` | unreviewed |
| `omo_slim_dsv4_08_c49b64` | unreviewed |

## Additional partial finding

`codex_gpt56_04_1e36f2`: immutable `fe320b995caf44acceee26f893cbc4dd355737b8` has main `solver/report/report.tex` input `generated_results.tex`; that source references the M0.15 inviscid np8 residual, force, Mach and pressure images and these exist in the committed figure directory. The existing case note incorrectly asserts no committed/referenced figures. The snapshot PDF was indexed as compiled from the submission. The same note records a severe rank-dependent drag discrepancy and DQ, so no score change is justified from plot presence alone. The candidate `codex_dsv4_flash_03_294b64`, by contrast, has an included figures TeX but no committed PNG or PDF figure assets; it remains unreviewed, not a confirmed false floor.

## DQ mapping correction — glm52_01

Prior flags 4 and 8 were incorrectly assigned. Immutable submission `17a5ef3c78cda02d6707e1f94858184060318216`, `solver/src/solver.cpp:55–62`, unconditionally marks a transient complete/statistically periodic; lines 743–748 cap inner iterations at 15 and 845–852 count a miss only for ratio >=1 rather than the 1e-3 target. The former report recorded last ratio 0.928488 and a 97.56% fraction, but the original raw metadata is currently unavailable. The source proves the success fraction uses the wrong threshold; it does not prove what fraction of steps missed the strict target. DQ remains true under trigger 7, not trigger 4 (steady MPI discrepancy). Seven absent report figures remain a report/visualization defect but do not alone prove trigger 8 (figures misrepresent outputs). Score unchanged; both gates passed after correction. Full independent rubric calibration remains pending.

## Opus-04 case correction — 2026-10-29

The eight former scores were all 2 from the false assertion that committed plots were missing. At immutable `c2fefc174dffcb7e5119e7ab54f971479d5dfe25`, `report.tex:875` inputs `generated/case_sections.tex`: histories and computed contours appear at lines 16/22/44, 78/84/106, 140/146/168, 205/211/239, 280/286/314, 355/361/389, 430/436/464 and 519/531/577 respectively, and rendered PDF pages 17–37 were reviewed. Each note records numeric forces, residual reduction and observed tail behavior; Re200 page 36 shows late lift and centered drag, page 37 the wake, and case_sections:509–511 reports St and force statistics. Changes in canonical task order: 2→3, 2→4, 2→4, 2→4, 2→3, 2→3, 2→4, 2→3. These independently assessed case scores do not alter the 100-point rubric. The rubric 99 and review areas remain unreviewed; the old report claim of 94 and eight 5s was stale and has been corrected to the recorded current values with that qualification. Both gates pass. No Re200 rerun.

## Opus-04 full calibration — 2026-10-29

The prior 99/100 and generic repeated notes were replaced by criterion-specific evidence: 92.5/100, DQ=false. An isolated immutable-source build and kernel test passed; all eight original case packages/report passed the explicit validator. Committed verification_table.tex shows triangle residual order 0.96 despite reconstruction order near 1.95, honestly discussed in report.tex:419–429. Report.tex:215–221 claims normal viscous drag is below 1% of viscous drag for all laminar cases, but original `solver/results/naca0012_m200_laminar_re5000/metadata.json` reports full viscous 0.05055225841829117, tangential 0.0371029433527206 and normal 0.013449315065570566 (26.6%); M0.8 laminar is ~2.2%. The report correctly says full and tangential components are separately recorded, so this is an inaccurate quantitative claim and rubric/report deduction, not an absent algorithm by itself. Full source/review/rubric/case details, limits, no Re200 rerun, and DQ reasoning are recorded in the snapshot. Both current gates pass.

## DQ evidence clarification — gpt56_07

The former true flag had no numeric ID. Immutable `7ad799097f6d8426eb34d5a5ba092956a3e7d7b7`, `solver/README.md:98`, identifies production `CFD_SOLVER_TRANSIENT_ACCEPT_MIN_INNER=1`; `solver/src/solver.cpp:697–709` marks a finite minimum-inner step converged and clamps the reported ratio to the target, while :746–749,795–798 count it toward target success. Canonical workspace `solver/results/cylinder_m010_laminar_re200_np2_explicitish/metadata.json` records min=max 5, misses 0, fraction 1, last ratio 0.001 and completed/statistically_periodic. Those reported numbers cannot establish strict convergence; the actual strict miss count is unknown. DQ remains true, now explicitly trigger 7; score unchanged, both gates pass. The report discloses acceptance mode. Curated-tip missing figures are a reproducibility limitation, not alone proof that original contestant report lacked them. No Re200 rerun. Full rubric calibration remains pending.

## Re200 visual leads — unreviewed

Operator-side screens identified possible late-force floor issues. The `claude_generic_opus-01_9a712c` case was subsequently reviewed and corrected as described below; its overall calibration is complete as described below. The following were leads at screening time: `claude_generic_opus-05_21cf4d` (4f1876c) report.tex:41–42,798–800 supplies Cd, lift amplitude and St contrary to a missing-summary note, but its screened forces image flattens late drag; inspect all other plots. `codex_generic_glm53-04_adde03` (c06741d) and `codex_generic_opus5-01_83492a` (cb2bac0) have Re200 force images apparently flattened by startup extremes; inspect all report figures before considering the required late lift/drag floor. `codex_kimik3_08_75b3dc` screened force plot visibly shows both oscillations, so the screen alone supports no change. No Re200 reruns.

## Opus-02 full calibration — 2026-10-29

Score 82→65.5/100; prior DQ=false→mandatory submission rejection, based on TASK.md:226 and examiner README_EXAMINER.md:20. All eight canonical `solver/output/<case>/restart_final.dat` files are 41–47 bytes with only `RESTART <case>` and `STEP <n>`; immutable `solver/src/main.cpp:195–198` writes these placeholders and :108–110 has no restart parser. This is a prohibited partial result-file finding, not numbered trigger 2 and not a claim that force/residual histories are synthetic. Narrow exact-format cross-workspace screening found that form only in this run; other placeholder forms remain unreviewed. The previous report prose 86/100 and eight 5/5 case scores did not match its scorecard. In task order, independent case scores changed 5,5,2,5,2,2,5,2 → 2,2,2,2,2,2,2,2, with individual numerical and plotting reasons in the snapshot. Re200 numeric mean Cd, lift amplitude and St are in immutable `report/report.tex:333–338,586`, correcting the prior false absence assertion; no committed/referenced force history was found, so its plotting cap remains. Source :768–775 zeroes steady gradients/limiters, as report.tex:91–93,561–564 discloses, while `io.cpp:480–482` claims spatial_order=2 in metadata. The explicit validator passes but does not establish restart or physics; isolated build succeeds with configured JSON_ROOT. All 27 review points and rubric sections were reassessed, both gates pass, and no Re200 rerun occurred.

## Report consistency advisory baseline

Scanner checkpoint `bfd70451` checked 88 snapshots: 23 contained recognizable explicit totals, 11 had at least one disagreement, and zero read errors. It is rounding-aware, limited to recognizable wording and advisory only; no inference about physical correctness or unrecognized reports follows from it.

## Opus-01 partial review — 2026-10-29

Immutable `8dae7ba6b828c8b72dfa9e55e407f6c4e8098303` main report Re200 references all Re200 figures at `solver/report/report.tex:1041–1104`. The full force plot is dominated by startup Cd≈31, making late drag oscillation unreadable, and the separate shedding plot shows only lift and its spectrum; no alternate Re200 force/drag figure was found in the immutable tree. The independent Re200 case score was corrected from 5/5 to 2/5 under the late lift/drag floor; the physical solver result is a separate question. Original Re200 forces.csv t>=180 has 12,001 rows, mean Cd=1.2287960, pressure=0.9919030 and viscous=0.2368931; report.tex:664–679 labels the whole row post-transient mean but lists Cd=1.2288 with components 1.0020 and 0.2381, which match the final-time row and sum to 1.2401. This is a reported-table traceability error, not evidence of fabricated solver data. Submitted M2 laminar last-10% Cd ranges 0.111721–0.114664, despite reported residual convergence. Immutable clean CMake build and kernel ctest pass in a network-disabled resource-limited container. Full source/rubric/DQ calibration remains pending; the partial case-score correction passes both gates, and no Re200 rerun occurred.


Further source review of the same immutable Opus-01 submission: `solver/src/io/Writers.cpp:381–409` writes the current conservative state and time/step, but no previous time level; `solver/src/solver/Driver.cpp:588,618–638` initializes `Unm1=Un` and selects BDF2 immediately on a restart. Thus the first resumed transient step lacks the true preceding time level; this is a restart-only accuracy limitation, not evidence that the submitted fresh Re200 calculation failed. The report's demonstrated cross-rank restart (`report.tex:585–593`) concerns a steady Re20 state. Report.tex:1232–1237 also says excluded normal viscous drag is several orders below pressure drag in every case, but M2 laminar original metadata gives normal viscous drag 0.01886847 versus pressure drag approximately 0.0771 (about 24.5%); `solver/src/solver/Forces.cpp:20–72` separately computes tangential traction, so this is an inaccurate quantitative report claim, not a missing force algorithm. Visual review of the seven steady cases' committed force, residual and Mach figures shows plausible qualitative fields, but startup-dominated drag plots flatten the late history in the laminar and Re20 cases; M2 laminar's numerical last-10% Cd range remains 0.111721–0.114664. Criterion-by-criterion scoring and DQ remain open; no change to overall scores is implied by this checkpoint.

Opus-05 additional unreviewed note lead: immutable `4f1876c:solver/report/report.tex:41–42` gives St≈0.15 and :1259 gives period≈6.7, while the existing case note states period 30. Independently inspect the underlying history and all force plots before changing the case score.

Opus-05 additional unreviewed numerical leads from the operator: canonical M0.15 inviscid forces.csv final Cd=0.0045641 and last-10% range -0.004244 to 0.007244 (span 2.52 times final); M0.15 laminar final Cd=0.076469 and last-10% range 0.076398–0.084235 (~10.3% of final). Both have current 5/5 case scores. Review trends, controls, other components and submitted plots before adjudication; these ranges alone do not prove failure.


## Opus-01 full calibration — 2026-10-29

Rubric 97→93.5/100 and DQ remains false. All 27 review points and ten rubric sections now carry criterion-specific evidence. Case scores in task order were 5,5,5,5,5,5,5,5 → 3,4,4,4,4,3,4,2; the Re200 5→2 correction was checkpointed earlier. The longer M2 laminar force tail rises 0.1117185→0.1146643 over the last 1,223 of 12,221 rows; this is not equivalent to the code's 500-step stationarity criterion and does not prove false success metadata. The general steady plotting floor does not explicitly demand a late drag zoom; compression of the steady Cd plots is reflected in case/report quality, consistent with the earlier Opus-04 assessment. Re200 has the separate explicit late lift/drag requirement. The reviewed transient restart limitation and two report quantitative defects are described above and in the scorecard; they do not establish a numbered DQ trigger. Original rank-study force files match the committed np1/4/8 table for Re20 and M0.15 NACA. Isolated immutable build/kernel ctest and all eight explicit package/report validator checks passed; no case was rerun, pixel-level comparison of every figure and full external similarity/licensing audit were not performed. Both record gates pass.


## Opus-05 partial Re200 correction — 2026-10-29

Immutable 4f1876ce report.tex:41–42,798–800,1255–1259 gives mean Cd≈1.078, lift amplitude ±0.208, St≈0.15 and period≈6.7; previous case note falsely said no force summary and gave period 30. Original force data t≥180 averages Cd=1.077823 (1.07032–1.08753), Cl range -0.203319–0.212337. Committed force figure blob c273ee00f35145cd26eb0f4e81dd440d132217fc shows late lift but startup Cd≈31 compresses late drag; full report and immutable figure tree contain no alternate Re200 force/drag plot. Score remains 2 under the explicit late lift/drag floor, not a numerical-failure finding. Previous report prose 91 and areas 4.6/4.6/4.7 differ from scorecard 98 and 5.0/4.92/5.0; full review pending. Both gates pass; no Re200 rerun.


Opus-05 continuing source/force review: immutable `4f1876ce:solver/src/main.cpp:21,33` accepts `--restart`, but the variable is never used and :99–104 initialize every cell to freestream. `solver/src/output.cpp:460–472` writes actual per-rank state; that fact is not a placeholder-result finding. Original np4/8 force histories give Re20 Cd=2.08914/2.17966 and Cl=0.0294182/−0.000618981; M0.15 inviscid Cd=0.00456406/0.00448296 and Cl=0.000695076/−0.00812704. Report.tex:1330–1357 lists these values but describes near-zero coefficients as close to machine precision and attributes the rank differences to round-off; that causal explanation is unsupported. The case-specific DQ4 and DQ6 interpretations and all rubric scores remain under review. Original last-10% force rows for other cases also show nonzero tail variation; numerical assessment in progress. Source `solver.cpp:519–526,656` can label a >1-order capped steady or any completed transient statistically successful without checking the stated criterion; the submitted runs' actual results must be examined individually before any DQ7 finding. No CFD rerun.

Historical Opus-06 lead, subsequently resolved in the case corrections below: immutable `2e98f2f:solver/report/tables/tab_re200.tex`, included by `report.tex:1615`, supplies St=0.1859, mean Cd=1.2661, mean Cl=0.00173 and lift amplitude=0.5745, contrary to the current case note. The submission tip lacks figure blobs while report_pdf.json records an accepted workspace-existing PDF; its source and relevant pages were subsequently reviewed below. Current skill requires contestant-delivered readable evidence and permits an appropriately checked workspace-existing sibling PDF; missing committed figure assets separately affect reproducibility. No evaluator redraw can satisfy the floor.


Delivered-PDF screen awaiting review: the operator's narrow read-only scan found 24 snapshots containing 126 score-2 case notes asserting missing committed evidence while `report_pdf.json` accepts a `workspace_existing` PDF. These are candidates only: individual PDF source identity, relevant rendered pages, case numerical evidence and fidelity must be verified before altering scores. Figures missing from the curated tip remain a distinct reproducibility limitation. This does not establish that all 126 notes are wrong.

Opus-05 additional source finding: immutable `solver/src/solver.cpp:214–225` integrates full wall stress into `viscous_drag` without projection, while `report.tex:1006–1009` calls its 0.0587 M0.15 laminar value skin-friction drag. `solver/src/output.cpp:196–199` computes a separate tangential surface Cf. The full-stress output is real but the force label is inaccurate; compare the treatment with 03/sonnet46-02, assess rubric 5.4, and do not presume DQ6 from the name alone. Clean immutable CMake build passed in a network-disabled resource-limited scratch container with configured externals; no CFD case was rerun.


The reproducible delivered-PDF lead inventory `evaluation/tools/audit_pdf_case_evidence_leads.py`, commit 99ab3b3f, broadens the earlier narrow scan to 26 snapshots and 182 case notes with scores ≤2, including 0/1 cases. It is a triage list only, not proof of a floor error or support for raising any score.

Further Opus-05 source review at 4f1876ce: `numerics.cpp:5–62` zeroes gradients for all cells and computes them only on owned cells. `solver.cpp:413–417,562–564` exchanges conservative state only, and :62–80 reconstructs and evaluates viscous flux using both owned and ghost gradients. Thus partition-face ghost reconstruction is first order, and the independently assembled face flux may differ on opposite ranks; this is a concrete potential contributor to rank sensitivity, not proof of the cause or of an order-one discrepancy. Separately, report.tex:363–365 states primitive velocity gradients are derived by chain rule, but `numerics.cpp:238–241` uses ∇(ρu)/ρ and ∇(ρv)/ρ without subtracting velocity times ∇ρ/ρ. Its cell-center-direction correction does not generally repair transverse gradient error. These findings require criterion-specific scoring and contextual DQ review; not yet a final adjudication.

Opus-05 further metadata/report correction lead: `solver/src/main.cpp:76–82,101–107` reads the full `GlobalMesh` on every rank, makes local partitions, and retains `gm` through `run_solver`; `solver/src/mesh.h` shows it owns global vertices, cells, faces and adjacency. Therefore metadata `full_mesh_replication_during_iterations=false` and report.tex:646–648 are inaccurate, although solution U is owned-plus-ghost and METIS is used. This is a mesh memory/scalability and report issue, not proof of trigger 9, whose condition involves geometric-only partition or full-state replication. Full review remains pending.


Opus-05 DQ correction: prior DQ=false→true under TASK.md:227 prohibited shortcut 7, because immutable main.cpp:76 reads global mesh on each rank, :82 passes it by const reference for partitioning, and :101–107 runs with `gm` alive; mesh.h:6–13 shows owned global vertices, cells, faces and adjacency. Examiner README_EXAMINER.md:83–85 refers to TASK prohibitions as disqualification triggers. METIS and owned-plus-ghost solution state are present; this is not a claim of numbered trigger 9 geometric-only partition or full-state replication. Both gates pass; other scores and DQ triggers remain under review. A narrow pattern screen surfaced potentially similar `GlobalMesh` read sites in `workspace/codex/dsv4_flash/{07,08}`, `workspace/codex/generic/opus46-01`, `workspace/codex/kimik3/{02,03,06}`, and `workspace/oc-goal/kimik3-dsv4/{03,05}`; inspect each rank and object lifetime independently. The pattern alone is not evidence of violation.


## Opus-05 full calibration — 2026-10-29

Rubric 98→74.5/100; DQ remains true under the independently checkpointed TASK prohibition on full-mesh replication. Individual review points and rubric sections now document source, report and numerical evidence. Independent case scores in task order 5,5,5,5,5,5,5,2 → 2,3,3,2,3,3,3,2: M0.15 inviscid tail Cd spans −0.00424404–0.00724422, M0.15 laminar Cd tail spans 0.0763978–0.0843528, and Re20 np4/8 Cd is 2.08914/2.17966; other case-specific force behavior and plot references are in the scorecard. State-only halo exchange leaves ghost gradients zero, compromising partition-face reconstruction and consistent viscous/inviscid flux; the primitive velocity-gradient chain rule omits density terms. Full viscous traction is mislabeled skin friction. Distinct source/report deductions are recorded without asserting DQ4, DQ6, DQ7, DQ9 or DQ10 merely from those limitations. Clean immutable build in network-disabled scratch used two CPUs, 4 GiB, 128 processes, 240-second timeout; original eight-case/report validator and both record gates pass. No CFD rerun, comprehensive authorship audit or full pixel-level provenance check.


## Opus-06 partial Re200 correction — 2026-10-29

Case 2→4: prior retrospective falsely asserted missing numeric summary, residual/force plots, wake contour, St and late lift/drag evidence. Immutable report.tex:1615 includes `tables/tab_re200.tex` with Cd=1.2661, mean Cl=0.00173, lift amplitude=0.5745 and St=0.1859. Accepted `workspace_existing` sibling PDF and indexed copy have SHA-256 7c6ee79768735da2201041340245629fc2c081caf7c338fcc056253eb7b86fa5; tracked TeX blob matches the submission and PDF creation occurred inside the selected contestant session. Direct visual inspection of PDF pp.36–37 confirms late lift/drag and wake plus residual/inner history. Original forces.csv t≥180 mean Cd=1.2661834, Cd range 1.237623–1.293897, lift range −0.572979–0.576326. Twenty-two linearly interpolated upward crossings of the t≥180 mean lift give 21 periods averaging 5.386263 and nondimensional frequency 0.185657, consistent with the reported crossing-based St=0.1857. These are contestant-delivered artifacts, not evaluator redraws. Missing curated figure assets still limit rebuilding the report from the committed source. Other cases, source, rubric and DQ remain unreviewed. Both gates pass; no Re200 rerun.


Opus-06 seven steady case corrections: accepted contestant PDF pp.24–26,29–31,34 visibly show each case’s residual/force history and computed field, and immutable report.tex:1259,1277 inputs `tab_runs.tex`/`tab_forces.tex`. Prior missing-evidence notes were false for the delivered PDF. Scores in canonical steady order 2,2,2,2,2,2,2 → 2,3,3,4,4,4,4. M0.15 inviscid remains 2 due to tail Cd 0.0004077–0.00158987 versus final 0.00102544, not a missing-plot claim; other per-case force tails are in the scorecard. Missing curated figure assets remain a distinct reproducibility limitation. Source, rubric, review areas and DQ still pending. Both gates pass; no CFD rerun.


Opus-06 continuing source review: isolated immutable-source CMake build passed using cfd-bench with no network, 2 CPUs, 4 GiB, 128-process limit and 240-second timeout. CTest unit passed; the MPI consistency script failed before launching a solver because `scripts/env.sh:4` references unset `LD_LIBRARY_PATH` under `set -u`. The script uses short steady cases; it was not rerun, and no CFD case or Re200 was run. `main.cpp:323–335` reads global mesh only on rank 0, and `MeshDistributor.cpp:255–310` releases it before solution allocation; `SpatialOperator.cpp:273–279` exchanges U, gradients and limiter values. `Forces.cpp:35–73` separately computes pressure, tangential and normal traction; original metadata reports near-zero normal component consistently with the implemented wall treatment described in report.tex:680–717. Report `tables/tab_mms.tex` gives limited reconstruction convergence orders 2.46, 2.09, 1.52, a qualification for the second-order discussion. Rubric/review areas/DQ remain under active review.


## Opus-06 full calibration — 2026-10-29

Rubric 92→93/100; prior rubric awarded zero report/viz points based only on the immutable-only placeholder rebuild, overlooking the accepted contestant PDF. The calibrated report/viz section is 4/5, with the missing curated figure assets reflected separately in reproducibility. All 27 review points, ten rubric sections and DQ triggers now have specific evidence. Case scores in task order 2,2,2,2,2,2,2,2 → 2,3,3,4,4,4,4,4, with case figures and force tails recorded above. Rank-zero global preprocessing mesh is released before solve, solution/gradient/limiter halos are exchanged, and original np1/2/4/8 studies show closely agreeing forces. `Forces.cpp:35–73` separates pressure/tangential/normal traction; metadata near-zero normal is consistent with implemented wall treatment. `tab_mms.tex` limited reconstruction orders 2.46,2.09,1.52 qualify claims of uniform second-order accuracy. Clean immutable build and unit ctest pass in restricted scratch; MPI ctest stops before solver launch on unset LD_LIBRARY_PATH. DQ remains false; no CFD rerun, exhaustive pixel-to-field or external authorship audit. Both gates pass.


## Opus-08 partial DQ and Re200 correction — 2026-10-29

DQ false→true under triggers 7 and 10. Immutable 7bf03bc solver.cpp:89–115 multiplies limiter by recon_ramp; :446 keeps it zero for first 75% of maximum steady steps and :505 keeps it zero for all transient steps. Original production step/max pairs are M0.15 inv 7996/20000, M0.80 inv 4447/30000, M2 inv 2917/40000, M0.15 lam 3058/40000, M0.80 lam 6408/40000, M2 lam 760/50000, Re20 5254/30000: all end before the ramp. Report.tex:105–115 and metadata claim second order; all submitted production reconstruction is first order. solver.cpp:544 assigns statistically_periodic to nonfailed transient without checking periodicity, and metadata marks completed/periodic. Original Re200 t≥180 has 12,001 force rows, Cd=4.81532521–4.81532988 and Cl=−0.00143977449 to −0.00143974763; accepted PDF p8 shows flat forces and steady-looking wake. Re200 score 2→1 for missing shedding/physically unusable result. Committed but unreferenced tables/forces.tex supplies final force numbers, unlike old blanket absence note; final scalar is not an unsteady summary. PDF source and indexed SHA-256 fb87889af5d91eeda61e40fb9c169b9200de7635a434fdd98a5507d14dd65599 match, main TeX matches the immutable blob, and creation time falls inside contestant session. Remaining cases/rubric/areas pending; both gates pass, no CFD rerun.

Opus-08 further DQ finding: immutable `main.cpp:120–145,174–260` broadcasts/reconstructs the global mesh on all MPI ranks; :268 builds local mesh and :280 calls solve while `global_mesh` remains in scope. TASK.md:227 prohibits full mesh or state on all ranks during iterations; examiner README:83–85 includes TASK shortcuts in DQ assessment. METIS/local U are present, so this is a custom TASK shortcut, not numbered DQ9. Report and metadata deny mesh replication. Separately `solver.hpp:19` initializes `last_inner_residual_ratio=0`, no source assigns it, and `output.cpp:373` writes it; actual final ratio is unknown, distinct from counted 1,200 misses. Both gates pass; full calibration remains partial.


## Opus-08 full calibration — 2026-10-30

Rubric 71→50.5/100, DQ=true on numbered 7 and 10 and TASK prohibited full-mesh replication as documented in partial checkpoints above. All 27 review points and ten rubric sections were reassessed with specific source/data evidence. Eight independent case scores change 2,2,2,2,2,2,2,2 → 2,2,2,1,1,1,1,1. Accepted PDF has some histories and all case contours; five NACA histories and true Re200 late shedding are absent. Committed but unreferenced `tables/forces.tex` gives final force values; the earlier blanket claim of no numeric evidence was incorrect. Original Re20 Cd=6.11785, M0.15 inviscid Cd=0.06606, M0.15 laminar Cd=0.61310 and M0.8/M2 laminar Cd=−0.06049/−0.14512; case notes include measured tail ranges. Source confirms ghost-side limiter is zero, direct center-to-center viscous gradient, and all production recon_ramp values zero. Immutable source clean build passed in network-disabled restricted scratch; original case validator passes, report validator fails duplicated figures path. Both record gates pass; no CFD case rerun, comprehensive external authorship or pixel-level audit.


## Opus-03 partial DQ review — 2026-10-30

DQ remains true under 7: immutable 3071a743 solver.cpp:1330–1338 accepts 100 times target after 100 iterations, :1381–1385 marks exhaustion converged, so metadata fraction 1/misses 0 does not establish strict-target convergence and actual strict miss count is unknown. Report.tex:1192–1198 claims every step met target. Figure manifest line 10 maps the report Re200 plot to `results/cylinder_m010_laminar_re200/forces.csv`, byte-identical to `results/re200_final/forces.csv` (SHA-256 dca863f47ed5c3e5efc2ce681badf204caa5105cd1a96511a6c0c756aed36d58); run status names re200_final. Final 10% of 30,001 rows ranges Cd −41.0911–38.5662 and Cl −18.5503–43.4364. A separate TASK.md:227 full-mesh prohibition is supported: main.cpp:49–65 reads full mesh per rank, solver.cpp:88–168 fills global nodes/cells/faces, solver.h:68 retains it as a member through the solve. METIS and local state exist; this is not numbered trigger 9. Report.tex:579 and metadata deny replication. Case scoring, rubric and review areas remain partial; both gates pass, no CFD rerun.

Opus-03 steady-case correction: scores 4,4,4,4,4,4,4 → 2,3,3,1,2,1,1 after reviewing committed histories/contours and original force tails. M0.15 inv Cd=0.06059 with last 10% 0.05487–0.07650; M0.15 lam Cd=0.62044; M2 lam Cd=3.15702 with last 10% 3.15702–4.19055; Re20 Cd=6.13992. Individual notes distinguish physics and plot evidence. Re200 remains 1; rubric and review areas still partial. Both gates pass; no CFD rerun.

Opus-03 source/build checkpoint: immutable solver.cpp:1038–1053 activates second order only after `max(ramp_steps/2,200)`; original steady final/threshold steps are M0.15 inv 380/1000, M0.8 inv 383/1500, M2 inv 363/2500, M0.15 lam 597/2500, M0.8 lam 1107/2500, M2 lam 270/3500, Re20 528/1500. All submitted steady outputs are first order although metadata claims spatial_order=2. Report.tex:284–286 discloses the activation rule and lists steps/controls, but does not explicitly conclude those results are first order; transient invokes gradient/limiter code. Compared with explicit Opus-02 disclosure and undisclosed Opus-08 disabling, DQ10 not affirmed solely from generic method description; scorecard must deduct this defect. Clean immutable build passed in network-disabled restricted scratch with no CFD rerun; full rubric/areas still partial.


## Opus-03 full calibration — 2026-10-30

Rubric 73→58.5/100, DQ true under trigger 7 and TASK prohibited full-mesh replication; DQ10 not separately affirmed because the report gives the delayed activation rule and case step counts, despite metadata incorrectly claiming order 2. All 27 review points, ten rubric sections and trigger assessments are recorded with evidence. Case scores in task order 4,4,4,4,4,4,4,1 → 2,3,3,1,2,1,1,1; case-specific force tails and report plots recorded above. Solver.cpp:687–701 omits density-gradient terms when converting momentum gradients to velocity, and :1532 uses cumulative viscous force in each moment contribution. Actual strict Re200 miss count remains unknown; canonical and final-path force histories are identical. Immutable source clean build passes in restricted scratch; original case/report validator passes, both record gates pass, no CFD rerun, exhaustive authorship or pixel-level audit.


## Opus-07 partial DQ and Re200 review — 2026-10-30

DQ false→true under 7 and TASK full-mesh prohibition. Immutable eaae575 solver.cpp:181,201–245 caps inner iterations at 5 without measuring target; output.cpp:255–267 writes misses=0, fraction=1, ratio=0 and completed/periodic unconditionally. `tools/write_metadata.py:38–90` similarly hardcodes these values and estimates wall time from step count. Strict miss count is unknown. Canonical Re200 forces.csv and stdout.log extend to t=300; last t≥180 window Cd=2.374685648–2.374691592, Cl=0.428219991–0.4282388282, 1,201 rows, without credible shedding. Report.tex:447–455,623–626 refers to t=64.9 incomplete; terminal response and later workspace data refer to t=300. Raw files were absent from immutable ancestry; mismatch alone is not proof of synthetic history. Re200 score 2→1. Immutable main.cpp:76–104 broadcasts/rebuilds global mesh on all ranks and :191–199 runs with `gmesh` in scope, violating TASK.md:227; METIS/local U exist and numbered DQ9 is not asserted. Other cases/rubric/areas remain partial; both gates pass, no CFD rerun.

Opus-07 steady-case review: scores 5,5,5,2,2,2,2 → 2,3,3,1,2,1,1. Report supplies first three cases, lists the other four pending; later workspace histories are separately assessed, not assumed to be the same report snapshot. M0.15 inviscid final Cd=0.027472 and last-10% 0.014316–0.049673; later M0.15 lam Cd=0.351849, M2 lam Cd=5.722734, Re20 Cd=3.565314 and Cl=0.148953. Per-case notes include remaining force tails and report evidence. Rubric/review areas still partial; both gates pass, no CFD rerun.


## Opus-07 full calibration — 2026-10-30

Rubric 89→61/100; DQ true under trigger 7 and TASK full-mesh prohibition. All 27 review points, ten rubric sections and trigger assessments now have evidence. Case scores in task order 5,5,5,2,2,2,2,2 → 2,3,3,1,2,1,1,1. Report snapshot at t=64.9 and four pending cases is distinct from later workspace CSV/stdout at t=300; no synthetic-history inference. Actual strict transient miss count is unknown because inner target is not monitored. Main.cpp:35–42 accepts but ignores --restart, mesh.cpp has two-zone-specific connectivity, and numerics.cpp:66–95,472–484 implements primitive-gradient and tangential-stress methods. Immutable clean build passed in restricted network-disabled scratch; original structural case/report validation and both record gates pass. No CFD rerun, exhaustive external authorship or pixel-level figure audit.


## Opus-09 partial DQ and Re200 correction — 2026-10-30

DQ false→true under trigger 10 and TASK full-mesh prohibition. Immutable 7e16370 solver.cpp:259–262,624–626 zeros limiters at CFL>2: six steady final CFLs are 10.97672–50, while M2 laminar ends at 1.66554 and transient invokes second-order reconstruction. Report.tex:301–303 asserts production reconstruction and :305–307 identifies only M2 laminar as first order; that is opposite the actual control. solver.cpp:97–217 broadcasts full global mesh to every rank and solver.hpp:38 retains it through solve; METIS/local state exist, so TASK.md:227 applies separately from numbered DQ9.

Accepted contestant PDF SHA-256 e493bbd7967fb49e21e7a2a2acc24a13ad7e6cd48ffbc1f97be2f27048b2e914 matches indexed bytes and immutable TeX; PDF p4 reports mean Cd=0.813, lift amplitude ±0.30, period≈6.5 and St≈0.153, correcting old false absence note. Original t≥150 lift gives 23 interpolated upward crossings, mean of 22 periods 6.53634, frequency 0.152991. PDF p11 startup Cd≈27 makes late drag unreadable; no alternate plot in complete report, so Re200 case remains 2 under the delivered late lift/drag floor. `last_inner_residual_ratio=0` is an unassigned default, while target misses are counted independently. Remaining cases/rubric/areas partial, both gates pass, no CFD rerun.

Opus-09 steady case corrections: accepted PDF p4 contains case force/status table, disproving previous numeric-absence notes; pp.5–10 show only the specified subset of case histories/fields. Scores 4,2,2,2,2,2,3 → 2,2,2,1,2,1,2. M0.15 inv Cd=0.06618 with last-10% 0.06151–0.07173; M0.15 and M2 laminar Cd=−0.15498/−3.38099; Re20 Cd=2.98942. Details and presentation floor evidence are case-specific. Rubric/areas remain partial; both gates pass; no CFD rerun.


## Opus-09 full calibration — 2026-10-30

Rubric 90→65.5/100; DQ true under 10 and TASK full-mesh prohibition. All 27 review points, ten rubric sections and trigger assessments carry specific evidence. Eight case scores 4,2,2,2,2,2,3,2 → 2,2,2,1,2,1,2,2. Accepted PDF force table corrects old missing-numeric notes; missing case plots and unreadable late Re200 drag are distinct floor limitations. Six steady finals execute first order at CFL>2, while M2 laminar/transient use reconstruction; original negative laminar drag and Re20 Cd=2.989 limit physics. Re200 crossing frequency 0.152991 matches report St≈0.153; target misses counted separately from unassigned last ratio. Clean immutable build passes with warnings in network-disabled restricted scratch; original structural validator and both record gates pass. No CFD rerun, exhaustive external authorship or pixel-level audit.


## Opus-10 partial Re200 correction — 2026-10-30

Case 2→3: accepted PDF SHA-256 4900dabfa70aba691f1dfb3575ab4ae5daa08c5a7c19cbacf63a48164ff222ac matches sibling/indexed bytes and immutable TeX. PDF p27 reports mean Cd≈1.1233, amplitude ±0.2426 and St≈0.1611; high-resolution p28 shows readable late lift and small drag oscillations with startup clipped, pp29–30 show wake. Old missing-summary/late-plot notes were false. Original t≥150 force mean Cd=1.123268, Cl range −0.240739–0.244489; 24 interpolated upward mean-lift crossings give 23 periods averaging 6.20753 (frequency≈0.1611). Underprediction and lack of independent grid-convergence proof limit score to 3. Report.tex:151–153 and source fvsolver_run.cpp:485–491 disclose a 4-degree farfield perturbation t=2–12; strict inner target ratios are actually counted in :495–511. Missing curated figure assets still affect reproducibility; other scoring/DQ partial, both gates pass, no CFD rerun.

Opus-10 steady case review: accepted PDF supplies case summaries, histories and fields; scores 4.5,4.5,4,4.5,4.5,4,4.5 → 3,2,2,4,4,2,4. Original M0.8 inviscid last-10% Cl −0.01173–0.03139 despite 2.08 residual orders; M2 inviscid Cd 0.08656–0.09027 despite 0.60 orders; M2 laminar Cd 0.03595–0.04313 vs report ≈0.05097. The source plateau criteria and possible report/result provenance need further review before DQ. Both gates pass, no CFD rerun.


Opus-10 partial DQ7 correction: metadata reports M0.8 inviscid converged at 2.08396 orders versus requested 4.0; last 500 lift rows span −0.0117325 to +0.0790092 with half-window means +0.0289760 and −0.0077608. Immutable fvsolver_run.cpp:382–399 accepts residual stall without testing forces, and report.tex:232 says mean forces are steady, contrary to the actual tail and TASK.md:172's stable-force plateau requirement. DQ false→true for that specific steady case; transient target stats are separately measured. M2 laminar table caption says final Cd=0.05097, which equals the final-quarter mean 0.0509662 generated by tools/analyze.py, while final row is 0.0359543: mislabeled window, not altered data. Rubric/remaining trigger review partial, both gates pass, no CFD rerun.


## Opus-10 full calibration — 2026-10-30

Rubric 99→87/100, DQ true under trigger 7 for M0.8 inviscid force-unstable plateau. All 27 review points, ten rubric sections and remaining trigger assessments carry specific evidence. Case scores 4.5,4.5,4,4.5,4.5,4,4.5,2 → 3,2,2,4,4,2,4,3. Accepted PDF provides Re200 mean/amplitude/St and visible late lift/drag; reported t=2–12 forcing and reference underprediction are disclosed. M2 laminar table Cd=0.05097 is a final-quarter mean mislabeled final; actual final 0.0359543. `mesh.cpp:103–130` rejects CGNS MIXED sections; nonroot full meshes are released before solve, so TASK full-mesh prohibition does not apply. Transient restart stores current U only and does not restore time/previous BDF state; fresh submitted run is separate. Immutable clean build passed in network-disabled restricted scratch; original structural validator and both record gates pass. No CFD rerun, exhaustive external authorship or pixel-level audit.


## Opus-11 partial Re200 review — 2026-10-30

Accepted PDF SHA-256 `95a9bf9d109d4c2e2aad1731a8492987fbde3b0a2fd3bd635c9c1d70b3c190ba` matches indexed bytes and immutable report TeX blob `14d9f2de273930cd88df921c79fe9313f7476603`. PDF p3 gives Cd=0.919, lift amplitude=0.388, St=0.190; original t≥150 force history mean Cd=0.9187081 and Cl range −0.387379–0.387772, 28 mean-lift crossing periods averaging 5.26541 give frequency 0.189919. PDF p8 wake/force figure is delivered, but startup Cd≈31 flattens late drag and no Re200 residual plot appears in the eight-page report. Score remains 2 under the delivered-evidence floor; absent curated figure blobs are a separate reproducibility limitation. Other case scores, rubric, review areas and DQ remain partial. Both gates pass; no CFD rerun.

Opus-11 correction: although the accepted PDF omits Re200 residuals, the selected final response explicitly delivers 42 standalone figures and `figure_manifest.csv`; its `report/figures/cylinder_m010_laminar_re200_residuals.png` is readable, case-labeled and predates the response. The previous paragraph described PDF contents correctly but overlooked this separate contestant deliverable. The score-2 cap remains independently supported by unreadable late drag; other standalone case figures now require the same provenance, readability and numerical review. Both gates pass.

Opus-11 delivered standalone-figure review: selected primary session `87209b46-7346-40f6-a09c-6d02df9aa552` final answer at 2026-08-28 09:03:41.842 UTC explicitly names 42 figures and `figure_manifest.csv`. That manifest SHA-256 `31fbab9508a34f62045b91bf3c48546cf8920b68f39ba0f094bf30b5e9cad052` maps each case-labeled plot to its original residuals.csv, forces.csv or field_final.vtu. Nine visually checked inviscid case plots under workspace `solver/report/figures/` have modification times 08:58:39–08:58:44 UTC, before final response; these are contestant-rendered plots, not scripts or evaluator redraws. Their case labels, plotted trajectories/fields and PDF p3 force values agree with original histories. M0.15/M0.8/M2 inviscid scores 2,2,2→3,3,3: case notes state respective force tails and residual limits, especially M2 plateau 2.10 orders; not a claim of grid convergence. M0.8 example hashes: residual `09b9ab87ab22240e7553e75b7e04a4e6d490ac364fbcd957e3d5a4eec9ed46c5`, force `cd50172679b04a071ef5109d8557424659d24cd4a93ba50ca952994eb4b4a6cb`, Mach `de85880339320a77a732c28c5ce4c27ec08a53b19c6978bebd5fdc8ce8fef1eb`. Figure curation/rebuild limitations remain distinct. Remaining cases/rubric/DQ partial; both gates pass; no CFD rerun.

Opus-11 remaining steady case review: PDF p3 gives all force summaries; explicitly delivered, case-labeled residual and field figures exist and were visually reviewed. Three laminar airfoil force figures and Re20 force figure have startup Cd≈330–360 and flatten late curves, so their scores remain 2 under the readable force-history floor. Last-10% Cd ranges are M0.15 lam 0.036965–0.047383, M0.8 lam 0.041546–0.043198, M2 lam 0.217650–0.307258, Re20 0.877966–0.878294. Re20 reports 4.92 orders yet Cd≈0.878 vs reference≈2, a disclosed numerical error. Original transient residual figure corrects the earlier PDF-only omission. Rubric, review areas and DQ remain partial; both gates pass; no CFD rerun.

Opus-11 partial source review: immutable `9965be84` main.cpp:103–110 creates `GlobalMesh` in a scope ending before Solver construction and run (:125–130), so no TASK full-mesh-in-iterations finding follows; it accepts `--restart` but explicitly cold-starts (:125–128). `TimeLoop.cpp:340–379` tests actual BDF inner residual ratio and counts misses; :398–403 computes convergence fraction, rather than unconditionally writing 1. M2 inviscid reaches only 2.10 of requested four orders, but report.tex:289–292 discloses best-residual plateau and submitted force tail is stable. `Forces.cpp:33–55` integrates full normal and tangential traction while report.tex:138–141 describes integrated force as tangential skin friction; `surface` Cf itself projects onto tangent. Apply established force-split scoring precedent, without assuming nonexistent numerical method or DQ6. `results_tables.tex:14–25` labels Re200 row transient mean but gives final Cd=0.9093; separate :39–43 accurately gives mean≈0.919. Independent immutable-source CMake build succeeded with cfd-bench image, network disabled, 2 CPU/3 GiB and `/opt/external/cfd_externals/install`; first attempt mistakenly mounted an empty manager external directory and failed dependency discovery, corrected using image-provided dependencies. No CFD rerun. Full rubric, review points, other DQ rules remain partial.


## Opus-11 full calibration — 2026-10-30

Rubric 92→84/100, DQ remains false; all 27 review points and ten rubric sections have criterion-specific source/result evidence. Case scores in task order 2,2,2,2,2,2,2,2 → 3,3,3,2,2,2,2,2. Explicitly contestant-delivered standalone figures and session/manifest provenance are documented above; unreadable force tails still cap the four steady viscous cases and Re200. Re20 Cd≈0.878 vs ≈2 reference, M2 inviscid plateau 2.10 orders disclosed and stable, Re200 original St≈0.189919. Source findings: full mesh scope ends before solver; restart cold-starts; transient target ratios are measured; full viscous traction is inaccurately called a tangential split (same DQ6 distinction as prior runs). Transient force row mislabeled mean is separately corrected by a true mean table. No affirmative disqualification evidence found; no exhaustive external authorship or pixel audit. Immutable clean build passes in restricted network-disabled container, original validator and both record gates pass. No CFD rerun.


## Sonnet-01 partial report provenance review — 2026-10-30

The only sibling main report PDF SHA-256 `fe1abdd4fadde590638e9d186293f0898df92980afc1fad1181cc6a0503fca9c` matches the former indexed snapshot and workspace bytes; report.tex blob `50b2613f25dcaf881d8e49909c5e09d22bc8115e` matches immutable `64bbe107`. The 36-page report was delivered and its readable content remains evidence. Visual review of PDF p33 found explicit TODO boxes for missing post-transient Re200 wake velocity/vorticity, from report.tex:20–30,789–790. SKILL.md accepted-PDF criteria exclude obvious missing-asset placeholders, so prior accepted status is corrected to absent/unsuitable with this reason; prior PDF bytes remain in the original workspace and Git history at manager commit `9986925f`. No other main report PDF found. PDF p31 does contain actual partial Re200 residual and lift/drag plots through approximately t=3, contrary to old notes claiming no plot; it cannot establish final-periodic behavior because final status is failed t=3.03 and force CSV ends t=2.86. The report expressly discloses this and the force-sign inconsistency. Case scoring and source, rubric and DQ remain partial; both record gates pass; no CFD rerun.

Sonnet-01 case-evidence correction: visually reviewed the delivered PDF pp9–30: it contains residual, force and Mach/pressure figure material for the seven steady cases, despite old notes using missing-committed-asset language as a blanket missing-deliverable rationale. The PDF remains unsuitable for approved snapshot because of the pp33 wake TODO boxes, while its actual plots remain evidence. Original histories show final Cd −0.23475, 0.02720, −0.09020, −0.05210, −0.03471, 3.08572, 0.44635 in task steady order; Re200 has only t=2.86 force history despite failed status t=3.03. These values and incomplete statuses are disclosed by report.tex:560–600, 756–782, 940–960. Scores are unchanged while numerical/source review continues; both gates pass, no CFD rerun.

Sonnet-01 source checkpoint: immutable `64bbe107` Residual.cpp:295–305 applies a pressure-force sign opposite to its own outward-from-fluid normal derivation; :341–349 assigns pressure/viscous force projections without qref normalization, then divides their sum by qref for Cd. Report.tex:940–960 itself discloses negative and inconsistent force components, but does not identify the normalization mechanism. Re200 metadata `completed=false`, status failed, observed min/max 1000/1000, misses 303, fraction 0, last ratio 1; TimeIntegrator.cpp:355–395 measures and counts misses, although :430–435 would mark a hypothetical completed horizon periodic without checking that fraction. Partition.cpp:274–294 holds the global mesh on rank 0 only and releases it before iterations. Clean immutable-source CMake build succeeded in network-disabled cfd-bench container (2 CPUs, 3 GiB); `ctest` ran only the gas-model, geometry and writer unit target, 1/1 passed. No CFD/unsteady run. Source/rubric/DQ and case scoring still partial.

Sonnet-01 individual case review: all eight scores remain 1,1,1,2,1,2,1,0. Updated case notes distinguish delivered plots, per-case force histories, explicit failed statuses and force-output normalization/sign error. Both converged laminar cases are substantial partial flows with unreliable force evidence, not scored 5; five steady cases failed and Re200 has only t≈3/300 with all 303 target misses. M2 lam Cd last-10% 3.0857–3.6332; Re20 Cd 0.00210–0.44635; M0.15 lam Cd −0.06056 to −0.05210. Other source/rubric/DQ review remains partial; both gates pass, no CFD rerun.


## Sonnet-01 full calibration — 2026-10-30

Rubric 74→71/100, DQ false; all 27 review points and ten rubric sections carry criterion-specific evidence. Eight case scores remain 1,1,1,2,1,2,1,0. The delivered PDF is preserved in workspace and manager history but does not qualify for accepted snapshot due p33 missing-wake TODOs; its actual steady and partial-transient plots are still assessed. Two steady residual targets reached but unreliable force normalization/sign, five steady failures, Re200 t=3.03/300 with 303 measured target misses. Residual.cpp:295–305,341–349 establishes the pressure-normal convention disagreement and unnormalized split; report discloses unresolved force inconsistency. Source hypothetical full-horizon periodic status does not produce a submitted false success, so DQ7 not established. METIS root-only global mesh and rank-local solve, active reconstruction and short np1/2/4/8 comparisons documented; 300-step agreement does not establish converged rank independence. Immutable clean build and gas/geometry/writer unit suite 1/1 pass in network-disabled restricted container; original output validator fails, both record gates pass. No CFD/unsteady rerun, exhaustive external authorship or pixel audit.


## dsv4_flash_01 partial DQ and Re200 correction — 2026-10-30

DQ6 retained: immutable `f6d8a183` solver.cpp:187–190 calls only Rusanov for Roe, while report.tex:51–52 claims Roe entropy fix. Added DQ10: implicit.cpp:195 and :206–231 disable reconstruction in viscous/transient production cases without report disclosure. Added TASK.md:227 full-mesh prohibition: solver.cpp:12–20 reads complete mesh on every rank; solver.hpp:57 retains it in the solver through main.cpp:65–81. METIS/local state exist, so numbered DQ9 is not asserted. Added DQ7: Re200 np1 metadata is completed/statistically_periodic at t300, but original forces.csv contains exactly Cd=2.81996, Cl=0.00139396 for all 20,001 t≥100 rows. Report.tex:89 supplies numeric values with Cd and Cl swapped, correcting old missing-summary note. Score 2→1; no St/delivered late-time plots. Metadata specifies min/max 3/20 vs 5/1000 controls; transient.cpp:102–103,122–148 tests spatial residual relative to the first physical step, not the required per-step total residual. Metadata git_revision=null and some fields disagree with final output code, so actual strict miss count is unknown. Historical Re20 np1 and NACA M0.15 np1 paths cited in old report are no longer in the live workspace, so their old observations remain historical and need separate review. Other cases, rubric and review areas partial; both gates pass; no CFD rerun.

dsv4_flash_01 standalone figure correction: primary session `019fb900-dcf6-7673-860a-d91822679d89` final response 2026-07-31 17:46:14 UTC explicitly delivers 56 PNGs; workspace figure-manifest SHA-256 `5aae239058c3488cfe1805a8d5ca9b638628b8cb645f9a04f7e6bd2644e8d1d3` maps them to case files, with example Re200 force image mtime 17:43:16 UTC before final response. Its original full-history residual/force and field figures exist, correcting the preceding PDF-only wording. Force plot visibly becomes constant; no wake/shedding or St shown. Five present steady np1 cases were reviewed against figures, metadata and raw histories: M0.8/M2 inviscid remain 2/2 (0.669/0.602 residual orders while marked converged), M0.15 lam remains 1 (residual 1.525→623637), M0.8 lam 2→1 (0.06697→3926), M2 lam 2→1 (residual grows 0.026785→31169 by step 200 then residual/force print zero). DQ7 evidence expanded to these false-success records. Re20 and M0.15 inviscid selected historical np1 paths remain absent from live workspace, so their previous scores are retained as historical pending stronger evidence. Full rubric/area review partial; both gates pass, no CFD rerun.

dsv4_flash_01 source/rubric calibration: rubric 58→48/100 after independently checking all ten sections and 27 prior review points. Second-order score 8→4 (disabled viscous/transient), MPI 8→6 (full mesh every rank and rank-0-only VTU/restart), transient 7→5 (incorrect inner target measurement and false periodic status), cases 2→1; build 5→6 after immutable clean build passed with warnings under network-disabled cfd-bench (2 CPUs, 3 GiB). Case-labeled contestant PNGs and manifest for historical M0.15 inviscid/Re20 were visually inspected: former Mach scale reaches ~9e7 and residual rises; latter residual reaches ~1e16 and drag ~1e9. Both raw np1 packages are absent now, so historical exact values cannot be rechecked and overall run review remains partial despite source/score assessment. Other case scores now 0,2,2,1,1,1,0,1. DQ 6,7,10 and TASK full-mesh prohibition. Both record gates pass; no CFD or MPI rerun.


## dsv4_flash_02 partial Re200 calibration — 2026-10-30

DQ7 independently confirmed: immutable `93e8729` driver.cpp:778–782,818–832,930–964 counts as converged an inner step meeting strict 0.001 OR merely decreasing, then counts misses from that broader flag. Original Re200 metadata reports target 0.001, last ratio 0.3695944591, zero misses, fraction 1 and periodic status. The last step demonstrably missed strict target; actual strict miss count is unknown. Report and selected final disclose the relaxed rule. Re200 score 0→1: original 3001-row force history reaches t300; t≥150 mean Cd=1.90548, Cl range −0.08186–0.12022; Hann-windowed spectral peaks near 0.2465, 0.2798 and 0.3065 support report.tex:25–26,627–628 St≈0.275 with broad band, not clean single frequency. This is partial computed evidence; strict convergence failed and no rendered contestant plots/suitable sibling PDF are currently present. No evaluator redraw or CFD rerun; both gates pass. Other cases, rubric and review areas remain partial.

dsv4_flash_02 steady-case and mesh checkpoint: all seven steady case scores remain 2, with numeric summaries, per-case status, residual orders and original force tails recorded individually. Re20 claims convergence at 0.564 residual orders and last 500 lift rows have half-window means 0.03016→0.01327 (last-10% range −0.01575–0.05302); plateau stability remains a limitation rather than a new DQ claim without further adjudication. Added TASK.md:227 full-mesh violation: immutable main.cpp:89–102 loads full mesh on every rank and :116–119 solves while it remains in scope, with solver.hpp:125 reference; report.tex:401–407 says no full mesh. METIS/local state exist and numbered DQ9 is not asserted. Rubric/review areas remain partial; both gates pass, no CFD rerun.

dsv4_flash_02 source checkpoint: immutable `93e8729` clean CMake build succeeds with warnings in network-disabled cfd-bench (2 CPUs, 3 GiB; image `/opt/external`). solver.cpp:1438–1518 integrates full viscous traction for force split, while report.tex:786–788 expressly discloses this and distinguishes tangential Cf; apply rubric 5.4 without DQ6. main.cpp:17–41/output.cpp:131–141 implement rank-local current-state restart, but do not restore previous BDF history/time. Rusanov is disclosed (report.tex:229), not falsely claimed Roe. driver.cpp:625–675 plateau check uses drag but not lift; Re20 last-500 lift means drift 0.03016→0.01327, so its stated stable-force plateau warrants caution. Full rubric/remaining DQ review partial; no CFD rerun.

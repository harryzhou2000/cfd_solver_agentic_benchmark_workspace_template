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
  every report or determine the correct score. For example, `opus-05` reports
  4.6/4.6/4.7 versus scorecard 5.0/4.92/5.0. Reconcile each when reviewing
  its substantive evidence; do not mechanically substitute one for the other.

## Existing catalog status

| Run ID | Review status |
|---|---|
| `claude_generic_opus-01_9a712c` | fully reviewed — 93.5/100, DQ=false; both gates pass |
| `claude_generic_opus-02_6fc51c` | fully reviewed — 65.5/100, mandatory placeholder-result rejection; both gates pass |
| `claude_generic_opus-03_68c1ac` | unreviewed |
| `claude_generic_opus-04_616ae8` | fully reviewed — 92.5/100, DQ=false; both gates pass |
| `claude_generic_opus-05_21cf4d` | partial — Re200 evidence note corrected; source and full scoring pending |
| `claude_generic_opus-06_40b02c` | unreviewed |
| `claude_generic_opus-07_c08c67` | unreviewed |
| `claude_generic_opus-08_853afe` | unreviewed |
| `claude_generic_opus-09_9cb2fc` | unreviewed |
| `claude_generic_opus-10_f6b586` | unreviewed |
| `claude_generic_opus-11_fe7eba` | unreviewed |
| `claude_generic_sonnet-01_96c411` | unreviewed |
| `codex_dsv4_flash_01_0c1996` | unreviewed |
| `codex_dsv4_flash_02_3447d3` | unreviewed |
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

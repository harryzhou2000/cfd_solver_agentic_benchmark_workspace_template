# Evaluation calibration ledger — 2026-10-29

This is a work-in-progress evidence ledger, not a completion claim. Passing structural gates or automated candidate triage do not establish calibrated scores. Reviewed means substantive manual review is complete across the applicable rubric, case results, scorecards and DQ evidence; corrected means a substantiated change passed both current gates. Partial items remain unreviewed for overall calibration. No unsteady or Re200 case has been rerun in this review.

## 30-minute review checkpoint

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

## New submissions — partial, not scored

| Run ID | Status | Evidence and remaining work |
|---|---|---|
| `codex_generic_dsv4.1f-1017-01_4f9379` | reviewed and recorded | 73.5/100, DQ=false; contestant `f156e96bdde30de9002d837731bc2fad337014a8`, manager snapshot `fc50e537`. Both gates passed. High steady drag, full-stress force split and noisy Re200 evidence are documented; no Re200 rerun. |
| `codex_generic_dsv4.1f-1017-02_df914f` | reviewed and recorded | 78/100, DQ=true under trigger 7 for M0.15 steady numerical limit cycle marked complete/statistically periodic; contestant `751d2ca9617f481a26cb7ff69c0844984a98d3ef`, manager snapshot `a9338172`. Both gates passed. Re200 forcing is disclosed and unforced shedding is not established; Re200 was not rerun. |
| `codex_generic_dsv4.1f-1017-03_dbc1f8` | reviewed and recorded | 78/100, DQ=false; contestant commit `89dee562643ce2b47f43e4a2b19989307f5b7858`, manager snapshot `f7bf749e`. Both integrity and completion gates pass. The report/source wall-traction mismatch is charged to viscous/report criteria, with the DQ 6 precedent distinction and unquantified normal contribution documented in the snapshot. Re200 was not rerun. |

Result branches preserve contestant history, which contains prohibited generated files; curation removes those files from the tip index only. Raw workspace evidence is retained. All three new snapshots have now been reviewed, recorded and committed separately; the historic catalog table below remains explicitly partial.

## Specific calibration finding

`claude_generic_opus-04_616ae8` is **partial review, correction pending**. Its `agent_scores.json` gives 99/100 and case scores 2, but `agent_report.md` says 94/100 and each case 5. The current gates pass despite this mismatch. Case notes assert missing committed/referenced plots. At submission `c2fefc174dffcb7e5119e7ab54f971479d5dfe25`, `solver/report/report.tex:875` inputs committed `solver/report/generated/case_sections.tex`; lines 20–57 of that included file reference M0.15 inviscid residual, force, Mach and pressure plots, and those figure blobs are present. Thus the blanket missing-plot assertion is false for this case. All eight cases appear in the included source, but each figure, summary, validity and case level still need review before any score is changed. The indexed 42-page PDF was visually screened at pages 17–37: case-specific residual and force histories and computed-field contours are displayed for all eight cases; Re200 pages 35–37 include a late force panel and wake contours. Case-specific readability, summary values, full physical and traceability review remain pending. A dependency-aware candidate screen additionally flagged `codex_gpt56_04_1e36f2`, `codex_kimik3_04_b353a7`, `oc-goal_kimik3-dsv4_02_0ceb76`, `codex_dsv4_flash_03_294b64`, `codex_kimik3_07_f42559`, `codex_kimik3_05_382d41`, `codex_dsv4_flash_14_80096b`, `oc-goal_kimik3-dsv4_01_940dcb`, and `codex_gpt56_12_e9972d`; these are leads, not findings or justified score changes.

## Existing catalog status

| Run ID | Review status |
|---|---|
| `claude_generic_opus-01_9a712c` | unreviewed |
| `claude_generic_opus-02_6fc51c` | unreviewed |
| `claude_generic_opus-03_68c1ac` | unreviewed |
| `claude_generic_opus-04_616ae8` | partial — case scores corrected; overall rubric and reviews unreviewed |
| `claude_generic_opus-05_21cf4d` | unreviewed |
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
| `codex_gpt56_07_b7c2c8` | unreviewed |
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

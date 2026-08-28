# Last-two Incremental Rounds — Retrospective Calibration (2026-08-28)

Scope: exactly the 33 immutable run IDs in `/tmp/cfd-last-two-increment-ids.txt`:
the 26 assigned entries in `EVALUATION_INCREMENTAL_DONE_MANIFEST_20260827.md`
and the seven entries in `EVALUATION_INCREMENTAL_DONE_MANIFEST_20260828.md`.

## Method and decision boundary

The current benchmark rubric and the evaluation skill were reread, together
with the 98-point `codex_gpt56_03_93b257` and 27-point
`omo_slim_dsv4_03_2fbbe7` anchors. The full current 75-snapshot comparison
population spans 26--99 (mean 74.76); it was used only to resolve genuine
judgment ambiguity, never as a quota. For every target this calibration read
the current `agent_scores.json`, scorecard notes, eight independent case
scores, DQ evidence, and report/limitation material. Section sums, 0--5 case
scores, and DQ facts were checked. No case was rerun, especially not Re200.

`before -> after` below means the authoritative snapshot total at the start of
this calibration to the final authoritative total. Thus all zero deltas are
deliberate reassessments, not omissions. The separate `manifest sync` notes
identify pre-existing prose/manifest totals that were stale and were corrected
without changing an underlying scorecard.

| Run ID | Before -> after (delta), DQ | Eight-case evidence / concise calibration rationale |
|---|---|---|
| `claude_generic_opus-01_9a712c` | 97 -> 97 (0), false | Eight 5s remain supported by validator-backed packages, t=300 Re200 and source/MPI evidence; only extensibility is deducted. |
| `claude_generic_opus-02_6fc51c` | 82 -> 82 (0), false; manifest sync 86 -> 82 | All eight packages validate; existing method/report/traceability deductions remain independently supported. |
| `claude_generic_opus-03_68c1ac` | 73 -> 73 (0), true; manifest sync 76 -> 73 | Seven cases are adequate/strong partial (4); Re200 is 1 because source accepts a 100x-relaxed target/max-inner exhaustion as converged (DQ7). |
| `claude_generic_opus-04_616ae8` | 99 -> 99 (0), false; manifest sync 94 -> 99 | Eight 5s and all ten section totals are internally consistent with validated packages/source; retained one-point extensibility deduction. |
| `claude_generic_opus-05_21cf4d` | 98 -> 98 (0), false; manifest sync 91 -> 98 | Validator-backed complete packages and t=300 Re200 support eight 5s; existing two points of implementation limitation remain. |
| `claude_generic_opus-06_40b02c` | 92 -> 92 (0), false | Results are strong, but missing-asset report rendering remains a report/visualization limitation; Re200 remains delivered-only evidence. |
| `claude_generic_opus-07_c08c67` | 89 -> 89 (0), false | Eight readable cases remain 5s; the existing report/traceability and implementation deductions are not double-counted. |
| `claude_generic_opus-08_853afe` | 71 -> 71 (0), false | Raw packages support partial/adequate cases, while doubled `figures/` paths and the short incomplete report justify report/visualization deductions. |
| `codex_dsv4_flash_14_80096b` | 87.5 -> 87.5 (0), false | Per-case 4--4.5 allocations retain production reconstruction blending and undisclosed M2 gradient damping deductions. |
| `codex_generic_02_ed5ee6` | 36 -> 36 (0), false | Missing M0.15 evidence and incomplete Re200 horizon independently justify zeros/low case scores and low case-results credit. |
| `codex_generic_03_5f2165` | 30 -> 30 (0), false | Failed/not-converged production outcomes, invalid metadata, and Re200 failure at physical time zero sustain the near-zero case layer. |
| `codex_generic_04_16009f` | 33 -> 33 (0), false | Invalid `equation_set` and seven still-running cases support the zero case-results bucket without a false-success DQ. |
| `codex_generic_05_2599b1` | 40 -> 40 (0), false | Declared packages fail `partitioner` metadata and terminal evidence admits weak/first-order behavior; partial attempts retain limited case credit. |
| `codex_generic_opus46-01_3a72ea` | 72 -> 72 (0), false | Structural pass does not cure implausible cylinder forces or M2 status/report inconsistency; case deductions remain distinct from method deductions. |
| `codex_generic_opus46-02_d8fed8` | 48 -> 48 (0), false | Broken figure-manifest path plus pending M2-laminar/Re200 packages supports low independent final-case scores. |
| `codex_generic_opus5-01_83492a` | 91 -> 91 (0), false; manifest sync 92 -> 91 | M2 inviscid 2.55-order plateau and modestly under-resolved Re200 remain the evidence-backed deductions. |
| `codex_generic_opus5-02_cac77b` | 92.5 -> 92.5 (0), false | Existing limiter/terminal-first-order and documented Re200 accuracy deductions remain proportional. |
| `codex_generic_sonnet46-01_ac6940` | 73 -> 73 (0), false | Weak M2-laminar/Re200 evidence and heavily curated artifacts sustain low case/result/report credit. |
| `codex_generic_sonnet46-02_dac37b` | 80 -> 80 (0), true | M0.15 false successful status at 1.69 orders after 20k iterations substantiates DQ7 and the existing case-result deduction. |
| `codex_generic_sonnet5-01_540ddf` | 76 -> 76 (0), false | Missing required immutable figure assets correctly leave report/visualization at zero despite readable packages. |
| `codex_generic_vllm-qwen38-02_d47362` | 82 -> 82 (0), true | The submitted M2 production configuration's np4/np8 order sensitivity supports DQ4 and already-deducted MPI/case credit. |
| `codex_gpt56_09_d5760c` | 75 -> 75 (0), true | Multiple far-below-target steady statuses marked converged remain DQ7; per-case partial scores keep the evidence separate. |
| `codex_gpt56_10_797ac3` | 90 -> 90 (0), true | All seven 0.021--0.673-order steady plateaus marked converged remain decisive DQ7 evidence; delivered Re200 stays 5. |
| `codex_gpt56_11_14a0ed` | 26 -> 26 (0), false | Six canonical cases/report absent and the remaining directories lack metadata, so all eight case scores remain zero. |
| `codex_gpt56_12_e9972d` | 90 -> 90 (0), false | All packages validate and Re200 is delivered; retained source-only/no-rerun uncertainty prevents unsupported full credit. |
| `codex_kimik3_03_09eb62` | 93 -> 93 (0), false | Target-reaching steady histories and delivered t=300 periodic Re200 support high but not perfect credit absent an evaluator rerun. |
| `codex_kimik3_04_b353a7` | 74 -> 74 (0), true; manifest/report sync 78 -> 74 | Four below-target steady plateaus marked converged sustain DQ7; a stale report verdict was corrected to the existing section sum. |
| `codex_kimik3_05_382d41` | 94 -> 94 (0), false | Stated steady targets and delivered periodic Re200 support the score; remaining method/MPI/plausibility deductions are retained. |
| `codex_kimik3_06_8208e5` | 94 -> 94 (0), false | Seven target hits, t=300 BDF2, zero target misses and reviewed report support the high score; no-rerun limit remains. |
| `codex_kimik3_07_f42559` | 72 -> 72 (0), true | Six below-target/max-step statuses marked converged establish DQ7 and sustain case credit below complete delivery. |
| `codex_kimik3_08_75b3dc` | 91 -> 91 (0), false | Force plateau, symmetry-broken M0.8, visualization limitations and no rerun retain existing deductions. |
| `codex_vllm-qwen38_03_da027b` | 35 -> 35 (0), false | No canonical case directories and validator stop at missing M0.15 metadata leave all eight case scores at zero. |
| `oc-goal_kimik3-dsv4_05_8d943e` | 91 -> 91 (0), false | Eight validated packages and delivered periodic Re200 remain credible; method/MPI/plausibility/traceability deductions are retained. |

## Record integrity and gates

All targets have populated ten-section rubric arrays, Code/CFD/Results
scorecards with notes, eight numeric case scores with notes, and explicit DQ
findings. The scorecards, immutable identities, submission/result branches,
telemetry, contestant evidence, and report-PDF records were not altered.

The stale human-report total for `codex_kimik3_04_b353a7` and six stale
manifest totals were corrected as manager-side record consistency changes.
All 33 target snapshots then passed `cfdeval check` and
`cfdeval check-complete`; the full comparison query returned 75 complete
population rows and all 33 target IDs.

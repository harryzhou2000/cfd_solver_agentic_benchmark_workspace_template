# Evaluation Retrospective Checklist

Created 2026-08-16. Each row is a canonical `agent_scores.json` snapshot. Audit status begins `pending`; `gpt56/08` is last because its snapshot has protected unrelated modifications.

| Snapshot | Audit | Old score/DQ | New score/DQ | Evidence-standard finding | Gates | Correction commit |
|---|---|---|---|---|---|---|
| codex_dsv4_flash_01_0c1996 | audited-no-change | 58/true | 58/true | Immutable attempt validator fails two cases; Re20/divergent and Re200 controls noncompliant; Roe claim absent in source supports DQ 6. | check + complete pass | pending batch |
| codex_dsv4_flash_02_3447d3 | audited-report-total-conflict | 73/true | 73/true | Scorecard/rubric total is 73 but prose still says 75; immutable audit/telemetry/validator support DQ 7 for admitted Re200 target miss reported successful. Report text correction pending next batch. | check + complete pass | pending batch |
| codex_dsv4_flash_03_294b64 | audited-no-change | 87/false | 87/false | Immutable eight-case/report validator pass and sole local Codex root verified; Re200 lacks delivered vortex shedding and curated report excludes referenced figures, already deducted. | check + complete pass | pending batch |
| codex_dsv4_flash_04_755869 | audited-no-change | 48/true | 48/true | Exact final response admits sibling-workspace result reuse (DQ 2); source gets partial credit but result/report credit remains zero; eight structural packages do not cure provenance. | check + complete pass | pending batch |
| codex_dsv4_flash_06_5b6b08 | audited-report-corrected | 63/false | 63/false | Immutable curated submission has no raw cases; written standard requires 0 (not null) for each absent case. Scorecard already has eight 0s; report prose needs correction from null to zero. | check + complete pass | pending batch |
| codex_dsv4_flash_07_aeae21 | audited-corrected | 84/false | 84/true | Written DQ 7 applies: Re200 metadata claims success/zero misses despite 0.3696 inner ratio vs 0.001 target and source's non-strict acceptance. | check + complete pass | pending batch |
| codex_dsv4_flash_08_efba3c | pending | 76/false | — | — | — | — |
| codex_dsv4_flash_09_526316 | pending | 55/false | — | — | — | — |
| codex_dsv4_flash_10_6468ba | pending | 86/false | — | — | — | — |
| codex_dsv4_flash_11_6e6ac8 | pending | 70/false | — | — | — | — |
| codex_glm52-m3_01_919b67 | pending | 72/false | — | — | — | — |
| codex_glm52-m3_02_ca46e4 | pending | 60/false | — | — | — | — |
| codex_glm52-m3_03_674665 | pending | 59/false | — | — | — | — |
| codex_glm52-m3_04_32f2d0 | pending | 65/true | — | — | — | — |
| codex_glm52-m3_05_f094c1 | pending | 60/true | — | — | — | — |
| codex_glm52_01_1307c9 | pending | 58/true | — | — | — | — |
| codex_glm52_02_06cf18 | pending | 76/false | — | — | — | — |
| codex_glm52_03_e7d38e | pending | 75/false | — | — | — | — |
| codex_gpt56_01_cb349f | pending | 97/false | — | — | — | — |
| codex_gpt56_02_38616e | pending | 92/false | — | — | — | — |
| codex_gpt56_03_93b257 | pending | 98/false | — | — | — | — |
| codex_gpt56_04_1e36f2 | pending | 86/true | — | — | — | — |
| codex_gpt56_05_7bee7c | pending | 85/false | — | — | — | — |
| codex_gpt56_06_97ae53 | pending | 89/false | — | — | — | — |
| codex_gpt56_07_b7c2c8 | pending | 70/true | — | — | — | — |
| codex_kimik3_01_09309d | pending | 84/true | — | — | — | — |
| oc-goal_gpt56-dsv4_01_24edd3 | pending | 95/false | — | — | — | — |
| oc-goal_kimik3-dsv4_01_940dcb | pending | 91/false | — | — | — | — |
| oc-goal_kimik3-dsv4_02_0ceb76 | pending | 94/false | — | — | — | — |
| oc-goal_kimik3-dsv4_03_28ff60 | pending | 72/false | — | — | — | — |
| oc-goal_kimik3-dsv4_04_60cc7d | pending | 72/false | — | — | — | — |
| omo_slim_dsv4_01_12c8d9 | pending | 85/false | — | — | — | — |
| omo_slim_dsv4_02_b52628 | pending | 36/true | — | — | — | — |
| omo_slim_dsv4_03_2fbbe7 | pending | 27/false | — | — | — | — |
| omo_slim_dsv4_04_d9ebd4 | pending | 64/true | — | — | — | — |
| omo_slim_dsv4_05_7a7141 | pending | 58/true | — | — | — | — |
| omo_slim_dsv4_06_d8d434 | pending | 70/false | — | — | — | — |
| omo_slim_dsv4_07_770c37 | pending | 42/false | — | — | — | — |
| omo_slim_dsv4_08_c49b64 | pending | 84/false | — | — | — | — |
| codex_gpt56_08_715d70 | deferred-conflict | 30/false | — | protected unrelated modified report/expense/measurement files | — | — |

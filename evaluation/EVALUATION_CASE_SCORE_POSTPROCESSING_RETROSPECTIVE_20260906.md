# Case-score post-processing retrospective (2026-09-06)

## Scope and interpretation

- Audited all 85 canonical snapshots mapped from root-level `workspace/**/done` markers (680 case scores).
- Evidence floor: a score of 3--5 requires a committed, case-specific numeric CL/CD summary, a referenced residual-history plot, a referenced force-history plot, and a referenced computed-field contour at the recorded immutable submission commit.
- Re200 additionally requires a committed shedding statistic and a referenced late-time periodic-force plot. It was never rerun.
- Raw workspace/result evidence remains useful for solver-functionality judgment but cannot replace missing immutable contestant report post-processing for this independent case-score layer.
- Per-case scores remain independent from the 100-point rubric and Code/CFD/Results scorecards; those layers were not mechanically recomputed.

## Evidence inventory

- Immutable-audit complete cases after manual false-positive/alias QA: 215/680.
- Cases below the immutable post-processing floor: 465/680.
- Reproducible temporary audit manifest: `/tmp/immutable_case_evidence.json` (not committed; all durable case notes carry the relevant commit/path/line evidence).

## Approved calibration samples

- High: `codex_generic_glm53-01_99a969`, NACA M0.15 inviscid 5 -> 5: CL/CD table and all three plot classes are committed and referenced.
- High: `claude_generic_opus-01_9a712c`, NACA M0.15 inviscid 5 -> 5: CL/CD values plus referenced histories and Mach/pressure contours are committed.
- Low: `codex_generic_glm53-03_d469fb`, M0.15 laminar Re5000 5 -> 1: report explicitly says Failed, gives nonphysical CD=-2.37, and omits committed case plots.
- Low: `omo_slim_dsv4_03_2fbbe7`, all cases 0 -> 0: no main report entry or credible immutable case-result package.

## Per-run score changes

| Run ID | Changed cases | Old sum | New sum | Material change | Gate |
|---|---:|---:|---:|---|---|
| `claude_generic_opus-01_9a712c` | 0 | 40 | 40 | notes rewritten with case-specific evidence; scores retained | pass |
| `claude_generic_opus-02_6fc51c` | 4 | 40 | 28 | naca0012_m200_inviscid: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `claude_generic_opus-03_68c1ac` | 0 | 29 | 29 | notes rewritten with case-specific evidence; scores retained | pass |
| `claude_generic_opus-04_616ae8` | 8 | 40 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `claude_generic_opus-05_21cf4d` | 1 | 40 | 37 | cylinder_m010_laminar_re200: 5->2 | pass |
| `claude_generic_opus-06_40b02c` | 8 | 39 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `claude_generic_opus-07_c08c67` | 5 | 40 | 25 | naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `claude_generic_opus-08_853afe` | 7 | 28 | 16 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 4->2 | pass |
| `claude_generic_opus-09_9cb2fc` | 5 | 29 | 19 | naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `claude_generic_opus-10_f6b586` | 1 | 34.5 | 32.5 | cylinder_m010_laminar_re200: 4->2 | pass |
| `claude_generic_opus-11_fe7eba` | 8 | 37.5 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 4.5->2 | pass |
| `claude_generic_sonnet-01_96c411` | 2 | 14 | 9 | naca0012_m015_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 4->2 | pass |
| `codex_dsv4_flash_01_0c1996` | 4 | 15 | 11 | naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m080_laminar_re5000: 3->2; naca0012_m200_laminar_re5000: 3->2 | pass |
| `codex_dsv4_flash_02_3447d3` | 7 | 28 | 14 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 4->2 | pass |
| `codex_dsv4_flash_03_294b64` | 5 | 28 | 15 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m200_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 5->2 | pass |
| `codex_dsv4_flash_04_755869` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_dsv4_flash_06_5b6b08` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_dsv4_flash_07_aeae21` | 0 | 29 | 29 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_dsv4_flash_08_efba3c` | 3 | 26 | 22 | naca0012_m015_inviscid: 4->2; naca0012_m200_inviscid: 3->2; naca0012_m200_laminar_re5000: 3->2 | pass |
| `codex_dsv4_flash_09_526316` | 0 | 4 | 4 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_dsv4_flash_10_6468ba` | 8 | 38 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_dsv4_flash_11_6e6ac8` | 7 | 27 | 14 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_dsv4_flash_12_7e965b` | 1 | 34 | 31.5 | cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_dsv4_flash_14_80096b` | 1 | 33.5 | 31 | cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_generic_02_ed5ee6` | 1 | 9 | 8 | naca0012_m200_inviscid: 3->2 | pass |
| `codex_generic_03_5f2165` | 0 | 1 | 1 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_generic_04_16009f` | 0 | 1 | 1 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_generic_05_2599b1` | 0 | 6 | 6 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_generic_glm53-01_99a969` | 1 | 38.5 | 36 | cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_generic_glm53-02_ff20c7` | 6 | 30.5 | 18 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 3->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_generic_glm53-03_d469fb` | 4 | 27 | 13 | naca0012_m015_laminar_re5000: 5->1; naca0012_m080_laminar_re5000: 4->1; naca0012_m200_laminar_re5000: 4->1; cylinder_m010_laminar_re20: 5->1 | pass |
| `codex_generic_glm53-04_adde03` | 0 | 31.5 | 31.5 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_generic_opus46-01_3a72ea` | 2 | 25 | 21 | naca0012_m080_inviscid: 4->2; naca0012_m080_laminar_re5000: 4->2 | pass |
| `codex_generic_opus46-02_d8fed8` | 6 | 18 | 12 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 3->2; naca0012_m080_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_generic_opus5-01_83492a` | 0 | 35 | 35 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_generic_opus5-02_cac77b` | 1 | 32.5 | 31 | cylinder_m010_laminar_re200: 3.5->2 | pass |
| `codex_generic_sonnet46-01_ac6940` | 6 | 22 | 16 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 3->2; naca0012_m080_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_generic_sonnet46-02_dac37b` | 1 | 28.5 | 27 | cylinder_m010_laminar_re200: 3.5->2 | pass |
| `codex_generic_sonnet5-01_540ddf` | 8 | 32 | 16 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 4->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_generic_sonnet5-02_6c0ac0` | 6 | 19 | 12 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 3->2; naca0012_m200_laminar_re5000: 3->2 | pass |
| `codex_generic_vllm-qwen38-01_2045e0` | 1 | 10 | 9 | naca0012_m200_inviscid: 3->2 | pass |
| `codex_generic_vllm-qwen38-02_d47362` | 1 | 22.5 | 21.5 | cylinder_m010_laminar_re200: 3->2 | pass |
| `codex_glm52-m3_01_919b67` | 3 | 21 | 16 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2 | pass |
| `codex_glm52-m3_02_ca46e4` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_glm52-m3_03_674665` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_glm52-m3_04_32f2d0` | 0 | 22.5 | 22.5 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_glm52-m3_05_f094c1` | 0 | 8 | 8 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_glm52-m3_06_a2dc07` | 5 | 17 | 13.5 | naca0012_m015_inviscid: 2.5->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m080_laminar_re5000: 2.5->2; naca0012_m200_laminar_re5000: 2.5->2 | pass |
| `codex_glm52_01_1307c9` | 5 | 17 | 12 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_glm52_02_06cf18` | 6 | 26 | 18 | naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 3->2; naca0012_m080_laminar_re5000: 3->2; naca0012_m200_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_glm52_03_e7d38e` | 7 | 24 | 15 | naca0012_m015_inviscid: 3.5->2; naca0012_m080_inviscid: 3.5->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 3.5->2; naca0012_m080_laminar_re5000: 3.5->2; naca0012_m200_laminar_re5000: 3->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_gpt56_01_cb349f` | 1 | 40 | 37 | cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_02_38616e` | 8 | 36 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 3->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_03_93b257` | 8 | 40 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_04_1e36f2` | 8 | 38 | 16 | naca0012_m015_inviscid: 3->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_05_7bee7c` | 1 | 31 | 30 | cylinder_m010_laminar_re200: 3->2 | pass |
| `codex_gpt56_06_97ae53` | 8 | 40 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_07_b7c2c8` | 4 | 22 | 15 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; cylinder_m010_laminar_re20: 3->2 | pass |
| `codex_gpt56_08_715d70` | 0 | 7 | 7 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_gpt56_09_d5760c` | 6 | 23 | 15.5 | naca0012_m080_inviscid: 3->2; naca0012_m200_inviscid: 3->2; naca0012_m015_laminar_re5000: 2.5->2; naca0012_m080_laminar_re5000: 3->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_gpt56_10_797ac3` | 1 | 16 | 13 | cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_gpt56_11_14a0ed` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_gpt56_12_e9972d` | 1 | 32.5 | 30 | cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_kimik3_01_09309d` | 7 | 36 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 5->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `codex_kimik3_03_09eb62` | 1 | 36 | 33.5 | cylinder_m010_laminar_re200: 4.5->2 | pass |
| `codex_kimik3_04_b353a7` | 1 | 24.5 | 22.5 | cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_kimik3_05_382d41` | 1 | 35.5 | 33.5 | cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_kimik3_06_8208e5` | 8 | 35.5 | 16 | naca0012_m015_inviscid: 4.5->2; naca0012_m080_inviscid: 4.5->2; naca0012_m200_inviscid: 4.5->2; naca0012_m015_laminar_re5000: 4.5->2; naca0012_m080_laminar_re5000: 4.5->2; naca0012_m200_laminar_re5000: 4.5->2; cylinder_m010_laminar_re20: 4.5->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_kimik3_07_f42559` | 1 | 19.5 | 17.5 | cylinder_m010_laminar_re200: 4->2 | pass |
| `codex_kimik3_08_75b3dc` | 0 | 35.5 | 35.5 | notes rewritten with case-specific evidence; scores retained | pass |
| `codex_vllm-qwen38_03_da027b` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `oc-goal_gpt56-dsv4_01_f83d02` | 8 | 39.5 | 16 | naca0012_m015_inviscid: 5->2; naca0012_m080_inviscid: 4.5->2; naca0012_m200_inviscid: 5->2; naca0012_m015_laminar_re5000: 5->2; naca0012_m080_laminar_re5000: 5->2; naca0012_m200_laminar_re5000: 5->2; cylinder_m010_laminar_re20: 5->2; cylinder_m010_laminar_re200: 5->2 | pass |
| `oc-goal_kimik3-dsv4_01_940dcb` | 1 | 39 | 36 | cylinder_m010_laminar_re200: 5->2 | pass |
| `oc-goal_kimik3-dsv4_02_0ceb76` | 1 | 40 | 37 | cylinder_m010_laminar_re200: 5->2 | pass |
| `oc-goal_kimik3-dsv4_03_eb4a9a` | 1 | 32 | 30 | cylinder_m010_laminar_re200: 4->2 | pass |
| `oc-goal_kimik3-dsv4_04_60cc7d` | 1 | 32 | 30 | cylinder_m010_laminar_re200: 4->2 | pass |
| `oc-goal_kimik3-dsv4_05_8d943e` | 6 | 35.5 | 21 | naca0012_m080_inviscid: 4.5->2; naca0012_m200_inviscid: 4.5->2; naca0012_m015_laminar_re5000: 4.5->2; naca0012_m080_laminar_re5000: 4.5->2; naca0012_m200_laminar_re5000: 4.5->2; cylinder_m010_laminar_re200: 4->2 | pass |
| `omo_slim_dsv4_01_12c8d9` | 1 | 34 | 33 | naca0012_m080_inviscid: 3->2 | pass |
| `omo_slim_dsv4_02_b52628` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `omo_slim_dsv4_03_2fbbe7` | 0 | 0 | 0 | notes rewritten with case-specific evidence; scores retained | pass |
| `omo_slim_dsv4_04_d9ebd4` | 0 | 3 | 3 | notes rewritten with case-specific evidence; scores retained | pass |
| `omo_slim_dsv4_05_7a7141` | 0 | 1 | 1 | notes rewritten with case-specific evidence; scores retained | pass |
| `omo_slim_dsv4_06_d8d434` | 7 | 28 | 14 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 4->2 | pass |
| `omo_slim_dsv4_07_770c37` | 1 | 4 | 2 | naca0012_m015_inviscid: 4->2 | pass |
| `omo_slim_dsv4_08_c49b64` | 7 | 28 | 14 | naca0012_m015_inviscid: 4->2; naca0012_m080_inviscid: 4->2; naca0012_m200_inviscid: 4->2; naca0012_m015_laminar_re5000: 4->2; naca0012_m080_laminar_re5000: 4->2; naca0012_m200_laminar_re5000: 4->2; cylinder_m010_laminar_re20: 4->2 | pass |

## Completion gates

- All 85 rows passed `record_agent_results.py`, `cfdeval check`, and `cfdeval check-complete` after the final immutable-evidence corrections.

# Session accounting audit — 2026-08-16

This audit used only each contestant workspace's bundled `.sessions` stores.
Canonical roots came from `agent_scores.json.session_selection.roots`; automatic
cwd discovery was not used as attribution authority. Recomputed token totals
and numeric per-model bundles were compared with the committed manager
snapshots. Evaluation scores, review JSON, rubric prose, and DQ decisions were
outside the migration scope.

## Codex

The canonical inventory contained 29 indexed Codex snapshots. Twelve required
regeneration with the secured owned-record extractor:

- `codex_dsv4_flash_01_0c1996`
- `codex_glm52-m3_01_919b67`
- `codex_glm52-m3_02_ca46e4`
- `codex_glm52-m3_06_a2dc07`
- `codex_glm52_01_1307c9`
- `codex_gpt56_01_cb349f`
- `codex_gpt56_02_38616e`
- `codex_gpt56_03_93b257`
- `codex_gpt56_04_1e36f2`
- `codex_gpt56_05_7bee7c`
- `codex_gpt56_06_97ae53`
- `codex_gpt56_08_715d70`

Independent explicit-root recomputation of every other canonical Codex
snapshot produced an identical total and identical numeric per-model
accounting:

- `codex_dsv4_flash_02_3447d3`
- `codex_dsv4_flash_03_294b64`
- `codex_dsv4_flash_04_755869`
- `codex_dsv4_flash_06_5b6b08`
- `codex_dsv4_flash_07_aeae21`
- `codex_dsv4_flash_08_efba3c`
- `codex_dsv4_flash_09_526316`
- `codex_dsv4_flash_10_6468ba`
- `codex_dsv4_flash_11_6e6ac8`
- `codex_dsv4_flash_12_7e965b`
- `codex_glm52-m3_03_674665`
- `codex_glm52-m3_04_32f2d0`
- `codex_glm52-m3_05_f094c1`
- `codex_glm52_02_06cf18`
- `codex_glm52_03_e7d38e`
- `codex_gpt56_07_b7c2c8`
- `codex_kimik3_01_09309d`

## OpenCode

All 13 canonical OpenCode snapshots were checked against their selected root
and recursive descendant closure in the bundled `opencode.db`. Session-row
counters matched assistant-message category sums and provider-reported costs.
Two snapshots required regeneration: `oc-goal_gpt56-dsv4_01_24edd3` for
cache-write input normalization, and `oc-goal_kimik3-dsv4_04_60cc7d` for an
in-session Kimi-to-DeepSeek model switch. The other 11 were unchanged.

## Commits and gates

- `1620872`: generic Codex fork-replay ownership fix.
- `d7c5ee9`: generic OpenCode request-model attribution fix.
- `67ee78b`: legacy structured Codex session-source normalization.
- `9a1a1d6`: two OpenCode snapshot migrations.
- `7162293`: first nine Codex snapshot migrations.
- `cf5826a`: three remaining explicitly rooted Codex migrations.

Every migrated snapshot passed both `cfdeval check` and
`cfdeval check-complete`. Score and review JSON remained unchanged.

## Follow-up: Codex in-thread model attribution

A second pass inspected the owned records of all 286 selected threads across
the same 29 canonical Codex snapshots. It tracked the latest owned
`turn_context.model` or `thread_settings_applied.thread_settings.model` at each
cumulative token delta. Parent history replayed into forked rollouts was
excluded before model inspection.

This found six snapshots where a thread changed models and the prior extractor
had assigned the thread's entire lifetime to the final `threads.model` value:

| Snapshot | Corrected model totals | Current estimate after correction |
|---|---|---:|
| `codex_dsv4_flash_08_efba3c` | BLSC Flash 289,674,332; DeepSeek Flash 7,656,299 | $10.3673 |
| `codex_dsv4_flash_09_526316` | BLSC Flash 201,372,019; DeepSeek Flash 12,975,815 | $7.3367 |
| `codex_glm52-m3_04_32f2d0` | BLSC GLM-5.2 1,027,022,657; DeepSeek Flash 1,569,943; MiniMax-M3 5,641,920 | $364.7693 |
| `codex_gpt56_01_cb349f` | GPT-5.6 Sol 166,639,034; Terra 46,126,083 | $131.9920 |
| `codex_gpt56_02_38616e` | GPT-5.6 Sol 517,332,169; Terra 60,714,493; Luna 15,079,616 | $366.3671 |
| `codex_gpt56_08_715d70` | GPT-5.6 Luna 5,155,445,204; Terra 322,497,440 | $685.5479 |

All six aggregate token totals are unchanged. The other 23 Codex snapshots,
including GPT-5.6 runs 05, 06, and 07, reproduced identical per-model token
bundles. The generic fix is commit `3ed5a23`. Scores, review JSON, rubric prose,
and DQ decisions remained byte-identical during regeneration.

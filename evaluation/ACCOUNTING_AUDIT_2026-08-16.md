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

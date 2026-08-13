# 50% Done-Run Evaluation Checklist

Selection rule: within each workspace series, evaluate the earliest
`ceil(number of done runs / 2)` run numbers. A root `done` marker is the
operator-reviewed normal-run authority for this legacy inventory. Completed
canonical evaluations count toward the quota and must not be repeated.

## Common gate for every pending run

- [ ] Read and follow `.codex/skills/cfd-benchmark-evaluation/SKILL.md`.
- [ ] Confirm root `done`; do not substitute artifact richness for this marker.
- [ ] Establish the immutable initial branch and initial commit. If the
      pre-run snapshot is absent, reconstruct them from reflog/refs/session and
      migration evidence, verify the commit object, and create the documented
      post-run snapshot before branch creation.
- [ ] Check the exact result branch locally and against the canonical upstream;
      never reuse a branch or choose another number.
- [ ] Curate and commit the contestant submission. Never commit `.sessions`,
      `.eval`, results, logs, restarts, field/visualization working data,
      generated PDFs, credentials, or build products. Only report-referenced
      curated PNGs are allowed.
- [ ] Audit the immutable submission commit and derive the canonical hashed run
      ID from that commit.
- [ ] Manually select the primary Codex/OpenCode root and complete descendant
      tree from the self-contained `.sessions` bundle; extract the exact final
      response before evaluation.
- [ ] Evaluate all rubric sections, validate all eight submitted cases and the
      report, record comments and DQ evidence, index/check the snapshot, and
      commit the manager snapshot separately. Do not push.
- [ ] Never rerun Re200. Do not repair migrated Docker-path `external` symlinks
      unless a permitted build verification genuinely requires an equivalent
      temporary path/mount; never commit that environmental accommodation.
- [ ] Preserve unrelated manager changes and report honest limitations.

## Series quotas and ordered work

### `codex/dsv4_flash` — 10 done, quota 5

- [x] 01 — `codex_dsv4_flash_01_0c1996`
- [ ] 02 — BLOCKED: provenance reconstructed (`codex/dsv4_flash/init` @ `6d76717d83cb1884055101a2bbeada962bdb8107`), but exact canonical-upstream collision query timed out twice with proxy bypassed on 2026-08-13; result branch must not be created until availability is established.
- [x] 03 — `codex_dsv4_flash_03_294b64`; 87/100; DQ no; submission `9772c16083472abe27089e17fe87bb5329e68f4d`; manager `b9cae66`; all eight cases plus report validator OK. Limitation: report PNGs referenced only through an included TeX file were excluded by strict direct-reference audit, so curated report is not self-contained; Re200 does not shed.
- [ ] 04
- [ ] 06

### `codex/glm52` — 3 done, quota 2

- [x] 01 — `codex_glm52_01_1307c9`
- [ ] 02

### `codex/glm52-m3` — 5 done, quota 3

- [x] 01 — `codex_glm52-m3_01_919b67`
- [ ] 02
- [ ] 03

### `codex/gpt56` — 8 done, quota 4

- [x] 01 — `codex_gpt56_01_cb349f`
- [ ] 02
- [ ] 03
- [ ] 04

### `codex/kimik3` — 1 done, quota 1

- [ ] 01

### `oc-goal/gpt56-dsv4` — 1 done, quota 1

- [ ] 01

### `oc-goal/kimik3-dsv4` — 3 done, quota 2

- [ ] 01
- [ ] 02

### `omo-slim/dsv4` — 8 done, quota 4

- [x] 01 — `omo_slim_dsv4_01_12c8d9`
- [ ] 02
- [ ] 03
- [ ] 04

## Legacy/session notes

- Runs without `.eval/env_snapshot.json` require explicit legacy provenance
  reconstruction; current branch/HEAD must not silently become initial state.
- Migrated Codex runs have rollout files and databases under
  `.sessions/codex`; migration manifests may show zero `session_ids` even when
  `codex_rollout_files` is nonzero. Inspect the rollout inventory directly.
- Migrated OpenCode runs use
  `.sessions/opencode-data/opencode/opencode.db`; manually classify the primary
  root and retain its descendants. Do not use a merged cwd-wide aggregate as
  the selected run.
- `workspace/oc-goal/gpt56-dsv4/01` is Docker-native/self-contained but lacks a
  migration manifest and pre-run snapshot; inventory both bundled harness
  stores, determine the actual harness manually, and reconstruct provenance.
- Existing canonical completed snapshots and result branches are immutable;
  do not regenerate them as part of this batch.

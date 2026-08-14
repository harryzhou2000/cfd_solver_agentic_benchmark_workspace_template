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
      never reuse a branch or choose another number. Delivered workspaces are
      expected to lack `origin`: restore the exact manager-repository canonical
      origin first, verify it, and record that local configuration repair.
- [ ] Curate and commit the contestant submission. Never commit `.sessions`,
      `.eval`, results, logs, restarts, field/visualization working data,
      generated PDFs, credentials, or build products. Only report-referenced
      curated PNGs are allowed.
- [ ] Audit the immutable submission commit and derive the canonical hashed run
      ID from that commit.
- [ ] Manually select the primary Codex/OpenCode root and complete descendant
      tree from the self-contained `.sessions` bundle; extract the exact final
      response before evaluation.
- [ ] For project Codex runs, regenerate metadata, expenses, measurements, and
      sessions from the same project DB/rollout paths and confirmed roots.
      Reject any snapshot where sessions say Codex but metadata says OpenCode,
      or where nonzero Codex usage is paired with zero expenses/measurements.
- [ ] Evaluate all rubric sections, validate all eight submitted cases and the
      report, and fill every Code/CFD/Results point and evidence note, all three
      weighted review overalls, all ten rubric sections, all eight case-score
      entries, and an explicit DQ verdict.
- [ ] Run both `cfdeval check` and `cfdeval check-complete`. Do not mark the
      item complete if either fails. A case-score `null` requires an explicit
      evidence-unavailable note; review point scores may not be null.
- [ ] Index/check the snapshot and commit the manager snapshot separately. Do
      not push. This checklist is shared evaluator state: inspect status/log and
      coordinate ownership before editing it; preserve concurrent evaluator
      updates.
- [ ] Never rerun Re200. Do not repair migrated Docker-path `external` symlinks
      unless a permitted build verification genuinely requires an equivalent
      temporary path/mount; never commit that environmental accommodation.
- [ ] Preserve unrelated manager changes and report honest limitations.

## Series quotas and ordered work

Current semantic-gate audit (2026-08-14): **12/22 target snapshots exist, but
0/22 currently pass `cfdeval check-complete`**. Existing rubric decisions and
immutable result commits remain evidence; `REPAIR` means finish the recorded
review evidence/sidecar protocol and re-index, not repeat the solver evaluation.
Reserve `[x]` for a snapshot that passes both gates.

### `codex/dsv4_flash` — 10 done, quota 5

- [x] 01 — `codex_dsv4_flash_01_0c1996`; 58/100 before DQ; DQ yes (trigger 6);
      submission `f6d8a1839a70c98d83a1e5baff9e2d6e111f7e2f`; manager pending.
      Completed originality from direct internal evidence and recomputed
      Code/CFD/Results overalls (3.32/3.34/1.35); both gates pass.
- [x] 02 — `codex_dsv4_flash_02_3447d3`; 73/100 before DQ; DQ yes.
      Curated submission `93e8729` passes audit and supersedes recovery
      `2376b11`; manager pending. All review scorecards/evidence and sidecar
      hashes refreshed; Code/CFD/Results 4.02/3.70/1.95; both gates pass.
      Eight case directories validate; curated report lacks excluded raw
      `run_manifest.csv`.
- [x] 03 — `codex_dsv4_flash_03_294b64`; 87/100; DQ no;
      submission `9772c16083472abe27089e17fe87bb5329e68f4d`; manager pending.
      Review scorecards/evidence and sidecar hashes refreshed; Code/CFD/Results
      4.77/4.62/3.85; both gates pass. Re200 shedding remains unverified.
- [x] 04 — `codex_dsv4_flash_04_755869`; 48/100 before DQ; DQ yes;
      submission `1b16448613a82d29f7f8f033b9216d5248a7db05`; manager pending.
      Direct sibling-result-reuse evidence retained; reviews 3.17/3.31/0.50,
      sidecar provenance, index, and both gates are complete.
- [ ] 06 — REPAIR `codex_dsv4_flash_06_5b6b08`; 63/100; DQ no;
      curated submission `e998060` supersedes recovery `8c757a9`; manager
      `ce57f3b`. All three review scorecards and report sidecar provenance are
      incomplete. Eight intentional null case scores have explicit unavailable
      evidence notes.

### `codex/glm52` — 3 done, quota 2

- [ ] 01 — REPAIR `codex_glm52_01_1307c9`; rubric/cases recorded,
      but `code.originality`, the Code overall, and weighted-overall consistency
      are incomplete.
- [x] 02 — `codex_glm52_02_06cf18`; 76/100; DQ no;
      submission `1e29ab3c`; manager pending. All 27 evidence notes,
      weighted overalls (4.20/3.96/4.25), and identity/report sidecar
      provenance were refreshed; both completion gates pass.

### `codex/glm52-m3` — 5 done, quota 3

- [ ] 01 — REPAIR `codex_glm52-m3_01_919b67`; rubric/cases recorded,
      but originality and weighted-overall consistency are incomplete.
- [ ] 02 — PENDING: provenance reconstructed (`codex/glm52-m3/init` @
      `746ef39`). Restore the verified manager canonical origin and run the
      normal exact collision gate; missing origin alone is not a blocker.
- [ ] 03 — PENDING: provenance reconstructed (`codex/glm52-m3/init` @
      `a14f203babe6794ad032aea760aed8e302e62b5a`). Restore the verified manager
      canonical origin and run the normal exact collision gate.

### `codex/gpt56` — 8 done, quota 4

- [ ] 01 — REPAIR `codex_gpt56_01_cb349f`; rubric/cases recorded, but
      `code.originality`, the Code overall, and final-response provenance in the
      report are incomplete.
- [x] 02 — `codex_gpt56_02_38616e`; 92/100; DQ no;
      submission `6183dc8fc067cd7dc4a4a2949ca53bd4f2b42820`; manager pending.
      All 27 evidence notes, weighted overalls (4.85/4.92/4.55), and
      identity/report sidecar provenance were refreshed; both completion gates pass.
- [x] 03 — `codex_gpt56_03_93b257`; 98/100; DQ no;
      submission `f06ea3fa63acbdbfce71ecd4debf603c355f751a`; manager pending.
      Recomputed Code/CFD/Results weighted overalls (4.85/4.92/4.95); both
      current structural and semantic completion gates pass.
- [ ] 04

### `codex/kimik3` — 1 done, quota 1

- [ ] 01

### `oc-goal/gpt56-dsv4` — 1 done, quota 1

- [ ] 01

### `oc-goal/kimik3-dsv4` — 3 done, quota 2

- [ ] 01
- [ ] 02

### `omo-slim/dsv4` — 8 done, quota 4

- [ ] 01 — REPAIR `omo_slim_dsv4_01_12c8d9`; rubric/cases recorded,
      but `code.originality`, the Code overall, and identity-sidecar provenance
      are incomplete.
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
- Existing canonical result branches are immutable. Repair only manager-side
  scoring/report/index artifacts for an existing snapshot; never rewrite its
  contestant result commit merely to satisfy the completion gate.
- A missing workspace `origin` is expected delivery state, not a blocker. Use
  the manager repository's verified canonical `origin`, add that exact URL as
  the workspace `origin`, and rerun the exact collision helper. Never overwrite
  a different existing remote silently, and never commit local remote config.

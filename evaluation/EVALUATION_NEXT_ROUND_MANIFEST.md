# Next-Round Done-Run Evaluation Manifest

Generated: 2026-08-15 (Asia/Shanghai)

This manifest contains every workspace that has a root `done` marker but no
canonical manager snapshot under `evaluation/outputs/*/run_identity.json`.
The inventory is **18 of 40** done workspaces; the other 22 already have
canonical snapshots. The evaluator may update checklist state and factual
notes in this file, but must preserve concurrent manager-repository edits.

Evaluation order is deliberate:

1. Evaluate the eight workspaces with authoritative pre-run provenance.
2. Do not start the other ten until the operator approves a legacy provenance
   reconstruction and a truthful `post_run` snapshot has been created and
   verified for that workspace.

The operator-selected run number is the two-digit workspace leaf shown here.
It must be preserved byte-for-byte in the result branch and run ID.

## Inventory and branch audit

All 18 target result branches were absent locally in their contestant
repositories and absent from the canonical manager upstream when checked on
2026-08-15. The read-only upstream audit used:

```text
https://github.com/harryzhou2000/cfd_solver_agentic_benchmark_workspace_template.git
```

Every contestant repository currently lacks `origin`, as expected for
delivered workspaces. The evaluator must leave that state unchanged. Before
branch creation, use the evaluation helper to query the exact verified manager
upstream URL directly, repeat the exact local and upstream collision checks,
and stop if either check is no longer clear. This read-only check needs no
per-workspace authorization. This manifest is an audit record, not a
substitute for the per-run collision gate.

## Wave A: authoritative pre-run provenance

These eight snapshots are authoritative and provenance-ready. Snapshot schema
v1.0 predates the `capture_phase` field and was emitted only during setup, so
it is classified as `pre_run`; v1.1 records the phase explicitly. “Ready”
means ready for the normal collision, submission-curation, and result-commit
workflow—not already evaluated.

| Status | Workspace | Snapshot | Authoritative initial branch | Authoritative initial commit | Result branch | Current branch / HEAD | Dirty tracked / untracked | Telemetry | Readiness note |
|---|---|---|---|---|---|---|---:|---|---|
| [x] `codex_dsv4_flash_08_efba3c`, 76/100, DQ=false | `workspace/codex/dsv4_flash/08` | `pre_run` v1.0 | `codex/dsv4_flash/init` | `e757c4e0bac4065c4a5ab7ead230b236e4a9984c` | `codex/dsv4_flash/08` | submission `e5d4a7a8a595c9e19db2e9551cfc393b9e646dba` | 0 / 780 | Codex | Both gates passed. Seven steady cases validate; Re200 is honestly incomplete and has no rerun; no submitted MPI rank comparison. |
| [x] `codex_dsv4_flash_09_526316`, 55/100, DQ=false | `workspace/codex/dsv4_flash/09` | `pre_run` v1.0 | `codex/dsv4_flash/init` | `e757c4e0bac4065c4a5ab7ead230b236e4a9984c` | `codex/dsv4_flash/09` | submission `11adaa1f2ddb33753cb6c9ed5d1c7e9c481050fb` | 0 / 48 | Codex | Both gates passed. Report compiles, but validator rejects failed canonical metadata; no unsteady rerun. |
| [x] `codex_dsv4_flash_10_6468ba`, 86/100, DQ=false | `workspace/codex/dsv4_flash/10` | `pre_run` v1.0 | `codex/dsv4_flash/init` | `e757c4e0bac4065c4a5ab7ead230b236e4a9984c` | `codex/dsv4_flash/10` | submission `47dff36bef32a0a978e276e3a2683e14444c2acc` | 0 / 2 | Codex | Both gates passed. Raw validator evidence preserved in immutable attempt `7152874` and intentionally excluded from curated branch. |
| [x] `codex_dsv4_flash_11_6e6ac8`, 70/100, DQ=false | `workspace/codex/dsv4_flash/11` | `pre_run` v1.0 | `codex/dsv4_flash/init` | `e757c4e0bac4065c4a5ab7ead230b236e4a9984c` | `codex/dsv4_flash/11` | submission `2cd0844d32d5d9200343d91d5ad13f09807702b3` | 0 / 926 | Codex | Both gates passed. Seven steady cases validate; Re200 metadata is absent/failed and was not rerun. |
| [x] | `workspace/codex/glm52-m3/05` | `pre_run` v1.0 | `codex/glm52-m3/init` | `a14f203babe6794ad032aea760aed8e302e62b5a` | `codex/glm52-m3/05` | `solver/attempt1-glm52` / `be266cd83d8c1c7811050158d0b83acdc59ed8f2` | 0 / 4 | Codex | `codex_glm52-m3_05_f094c1`; rubric 60/100, DQ true (production limiter/reconstruction disabled); raw case packages absent, no Re200 rerun. |
| [x] | `workspace/codex/gpt56/08` | `pre_run` v1.0 | `codex/gpt56/init` | `835bc07eaa03beeed2db88c13089b8e3e639f13b` | `codex/gpt56/08` | `solver/cfd-benchmark` / `4bd4b8a251bef2ea70ddd90f333b64b2d5b56d1a` | 1 / 9854 | Codex | `codex_gpt56_08_715d70`; 30/100, DQ false; seven steady packages reported validated, Re200 honestly failed and not rerun. |
| [x] | `workspace/oc-goal/kimik3-dsv4/03` | `pre_run` v1.0 | `oc-goal/kimik3-dsv4/init` | `7bb0b026bbc59386143a6f8c46ed56e9e687e900` | `oc-goal/kimik3-dsv4/03` | `solver/cfd-benchmark` / `8285817960158113d900fad3335bccef9c0f764f` | 0 / 1 | OpenCode | `oc-goal_kimik3-dsv4_03_28ff60`; 80/100, DQ false; all raw packages validate in immutable attempt, terminal response absent. |
| [x] | `workspace/oc-goal/kimik3-dsv4/04` | `pre_run` v1.1 explicit | `oc-goal/kimik3-dsv4/init` | `7bb0b026bbc59386143a6f8c46ed56e9e687e900` | `oc-goal/kimik3-dsv4/04` | `solver/k3-cfd-benchmark` / `7c1d0bf2856abe49342721ab9e591beb32abb746` | 0 / 2 | OpenCode | `oc-goal_kimik3-dsv4_04_60cc7d`; 80/100, DQ false; all raw packages validate in immutable attempt. |

## Wave B: blocked on post-run provenance reconstruction

These ten workspaces have no `.eval/env_snapshot.json`. They are **not**
pre-run and must not be silently treated as such. Their initial branch and
commit remain unavailable/non-authoritative until the operator approves the
reconstruction sources. Verify every source and commit object, then create a
snapshot with `--capture-phase post_run`, explicit reconstruction-source
descriptions, and `run_environment_available: false`. Capture-time host and
tool facts are diagnostics only; original harness/container versions remain
unavailable.

| Status | Workspace | Snapshot state | Expected result branch if provenance is approved | Current branch / HEAD | Dirty tracked / untracked | Telemetry | Blocker / curation risk |
|---|---|---|---|---|---:|---|---|
| [x] `codex_dsv4_flash_07_aeae21`, 84/100, DQ=false | `workspace/codex/dsv4_flash/07` | post_run reconstruction | `codex/dsv4_flash/init` / `ab943567cd55e707e760531ed7f2c914d09a0537` | `codex/dsv4_flash/07` / submission `a7ea108144b09b06e809d3c5e55b1b2333830947` | 0 / 2 | Codex | Both gates passed. Raw eight-case/report validation passed in detached attempt worktree; Re200 inner target is not credibly met and was not rerun. |
| [x] `codex_glm52-m3_04_32f2d0`, 65/100, DQ=true | `workspace/codex/glm52-m3/04` | post_run reconstruction | `codex/glm52-m3/04` | submission `8615cddbc8d507f20c559d9659eccbf41d8021f1` | 0 / 1 | Codex | Both gates passed. Submission omits all raw case packages; immutable attempt validates seven credible packages but M2 inviscid metadata says converged while report sanity marks failed; severe order-one steady MPI dependence. |
| [x] `codex_glm52_03_e7d38e`, 75/100, DQ=false | `workspace/codex/glm52/03` | post_run reconstruction | `codex/glm52/03` | submission `9532e19d7552900782e8835c441cbed2303178f5` | 0 / 2 | Codex | Both gates passed. All eight raw attempt packages and report validate in detached immutable evidence; curated submission excludes raw outputs. Re200 reaches t=300 but report admits no vortex shedding. |
| [x] `codex_gpt56_05_7bee7c`, 85/100, DQ=false | `workspace/codex/gpt56/05` | post_run reconstruction | `codex/gpt56/05` | submission `85d470dd16c1a8cae379513d3cbd5ca59f2ce030` | 0 / 1 | Codex | Both gates passed. Official validator passed seven steady packages and report in immutable attempt; Re200 is OOM-unverified due its 802 MB residual and was not rerun. |
| [x] `codex_gpt56_06_97ae53`, 89/100, DQ=false | `workspace/codex/gpt56/06` | post_run reconstruction | `codex/gpt56/06` | submission `55b24264cfb94fe01c32133e30885c75cb2e17bf` | 0 / 1884 | Codex | Both gates passed. Official validator passed exact eight case packages and report in immutable attempt `297126a`; Re200 t=300/periodicity evidence was reviewed but not rerun. |
| [ ] BLOCKED | `workspace/codex/gpt56/07` | missing | `codex/gpt56/07` | `codex/gpt56/init` / `0b925edb552ae31c3606748051f36b7ca24509e0` | 0 / 2 | Codex + OpenCode | Advanced init ref; manually disambiguate the primary harness and tree. |
| [ ] BLOCKED | `workspace/omo-slim/dsv4/05` | missing | `omo_slim/dsv4/05` | `omo_slim/dsv4/init` / `1234b7a7f77bcb07c669833bc8aeefd6517cc381` | 13 / 2 | OpenCode | Advanced init ref; exclude tracked telemetry/result changes. |
| [ ] BLOCKED | `workspace/omo-slim/dsv4/06` | missing | `omo_slim/dsv4/06` | `solver/cfd-benchmark` / `cb5b215d26906012e38ded4c4553a6ca8d633885` | 21 / 1 | OpenCode | Local init also advanced; exclude tracked telemetry changes. |
| [ ] BLOCKED | `workspace/omo-slim/dsv4/07` | missing | `omo_slim/dsv4/07` | `solver/cfd-solver-benchmark` / `011ab07a2d4e85207ccb0bda138061896f683301` | 0 / 26 | OpenCode | Exclude raw result files. |
| [ ] BLOCKED | `workspace/omo-slim/dsv4/08` | missing | `omo_slim/dsv4/08` | `solver/cfd-benchmark` / `ede9e4de59a1b0d70deb2eebfe39fa335a589a3e` | 7 / 5 | OpenCode | Exclude tracked raw result changes. |

Local refs/reflogs contain possible reconstruction leads, but they are not
authoritative initial commits and are intentionally omitted from the identity
columns above. The evaluator may document and verify them as evidence only
after an explicit operator provenance decision.

## Self-contained session sources

All 18 workspaces contain the harness-appropriate project-local database:

- Codex: `<workspace>/.sessions/codex/state_5.sqlite`, with bundled rollouts
  under `<workspace>/.sessions/codex/sessions/`.
- OpenCode: `<workspace>/.sessions/opencode-data/opencode/opencode.db`.

Never query the evaluator user's home session stores. Docker-isolated runs use
host path `workspace/<harness>/<model>/<number>` but normally record project
cwd `/workspace`; map that namespace only to the same workspace-local
`.sessions` bundle. Stored rollout paths are historical locators and must not
be followed outside `.sessions`. `codex/gpt56/07` has both database types, so
its primary harness/root requires manual classification rather than merging.

All 18 `external` symlinks are currently dangling on the host. Some preserve
the Docker `/opt/external` target and others a legacy sibling target. This is
runtime layout evidence, not an environment-phase signal. Do not repair the
canonical workspaces merely for evaluation; if a permitted build check needs
externals, provide an equivalent isolated container mount.

## Mandatory per-workspace completion gate

For each row, the evaluator must:

- [ ] Re-read `.codex/skills/cfd-benchmark-evaluation/SKILL.md`, confirm the
      root `done` marker, and establish authoritative immutable provenance.
- [ ] Recheck the exact local and upstream result branch. Leave a missing
      contestant `origin` missing and query the verified manager upstream URL
      directly; never invent or overwrite a URL.
- [ ] Curate explicit submission paths and commit `results: <result-branch>`.
      Never use blind `git add -A`. Never commit `.sessions`, `.eval`, raw
      results, logs, restarts, fields, visualization working data, generated
      PDFs, binaries, build products, or credentials. Only report-referenced
      curated PNGs under the report figure directory are allowed.
- [ ] Audit the immutable submission commit and derive the canonical hashed
      run ID using the exact operator number and immutable Git blobs.
- [ ] Manually select the primary root and all genuine descendants from the
      workspace-local session bundle. Regenerate all telemetry sidecars from
      that same tree and extract the exact terminal final response before
      evaluation. Record execution date from the selected root start.
- [ ] Read the contestant final response and required benchmark authorities;
      validate all eight submitted case directories and the report explicitly.
- [ ] Apply the evidence-escalation policy. Evaluator redraws may clarify
      readable submitted data but never improve report credit. Rerun a steady
      case only when solver-functionality evidence is genuinely missing.
      **Never rerun Re200 or any unsteady case.**
- [ ] Complete every Code/CFD/Results 0-5 point and evidence note, the three
      weighted review overalls, all ten independent 100-point rubric sections,
      all eight independent 0-5 case scores with case-specific reasons, and an
      explicit DQ verdict with evidence.
- [ ] Run `record_agent_results.py`, `cfdeval check`, and the mandatory
      `cfdeval check-complete`. Mark the row complete only after both checks
      pass and record the run ID, score, DQ verdict, submission commit, manager
      snapshot commit, and material limitations.
- [ ] Commit the manager snapshot separately. Do not push.

The 100-point rubric, Code/CFD/Results scorecards, and eight 0-5 case scores
are independent reporting layers and must not be added to or substituted for
one another.

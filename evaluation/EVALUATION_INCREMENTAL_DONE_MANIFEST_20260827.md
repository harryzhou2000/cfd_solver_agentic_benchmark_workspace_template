# Incremental Done Evaluation Manifest — 2026-08-27

This is the bounded evaluator manifest for exactly the operator-labelled done
workspaces assigned on 2026-08-27. It is separate from the prior incremental
checklist so concurrent historical entries remain untouched.

## Read-only preflight

- Re-inventory: all 26 listed paths exist, contain the legacy authoritative
  root `done` marker, and contain `.eval/env_snapshot.json` with a nonempty
  frozen initial branch and commit. No listed workspace already has a canonical
  `evaluation/outputs/*/run_identity.json` snapshot.
- Local target branches computed from each frozen initial branch plus the exact
  leaf label are absent. This is not a substitute for the required per-run
  exact canonical-upstream collision query immediately before branch creation.
- The resolved host checkout is `/home/harry/ssd1/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template`;
  the requested `/mnt/ssd-SATARAID5/...` path is its workspace alias.

## Calibration observations (not score templates)

- `codex_gpt56_03_93b257`: high confidence requires all eight readable,
  validator-supported cases, source/result consistency, and Re200 evidence at
  the production horizon; its unindexed identity/final-response sidecars remain
  explicitly qualified.
- `codex_gpt56_06_97ae53`: a strong score can coexist with post-run provenance
  and intentionally uncommitted raw outputs when the immutable attempt and
  curated source/report evidence are clearly separated; Re200 was not rerun.
- `codex_dsv4_flash_01_0c1996`: a structural package is not enough; direct
  report/source contradiction and failed/nonphysical cases justify DQ and
  independent per-case deductions.
- `omo_slim_dsv4_07_770c37`: incomplete readable deliverables receive
  case-specific zeros without a false-success DQ when the terminal response is
  candid.
- `oc-goal_kimik3-dsv4_04_60cc7d`: OpenCode attribution/final-response
  availability is independent of result completeness; do not manufacture a
  terminal answer or blend aggregate telemetry into primary-run measurements.

## Assigned inventory

| Status | Workspace | Exact label | Initial provenance | Result branch | Run ID / score / DQ | Gates | Notes |
|---|---|---|---|---|---|---|---|
| [x] | `workspace/claude/generic/opus-01` | `opus-01` | `claude/generic/init` at `ecf774c1…` | `claude/generic/opus-01` | `claude_generic_opus-01_9a712c`; 97/100; DQ=false | check + check-complete pass | Submission `8dae7ba6…`; selected Claude root `3a8e…` with five persisted subagents; raw packages/PDF excluded at result tip; no Re200 rerun. |
| [x] | `workspace/claude/generic/opus-02` | `opus-02` | `claude/generic/init` at `ecf774c1…` | `claude/generic/opus-02` | `claude_generic_opus-02_6fc51c`; 86/100; DQ=false | check + check-complete pass | Submission `6ed4241…`; sole Claude root `5e6d…`; unreferenced figures and raw packages excluded; no Re200 rerun. |
| [x] | `workspace/claude/generic/opus-04` | `opus-04` | `claude/generic/init` at `ecf774c1…` | `claude/generic/opus-04` | `claude_generic_opus-04_616ae8`; 94/100; DQ=false | check + check-complete pass | Submission `c2fefc1…`; sole Claude root; raw results and unreferenced figures excluded; no Re200 rerun. |
| [x] | `workspace/claude/generic/opus-05` | `opus-05` | `claude/generic/init` at `ecf774c1…` | `claude/generic/opus-05` | `claude_generic_opus-05_21cf4d`; 91/100; DQ=false | check + check-complete pass | Submission `4f1876c…`; seven Claude subagents; raw artifacts/unreferenced rank figures excluded; no Re200 rerun. |
| [x] | `workspace/claude/generic/opus-07` | `opus-07` | `claude/generic/init` at `ecf774c1…` | `claude/generic/opus-07` | `claude_generic_opus-07_c08c67`; 89/100; DQ=false | check + check-complete pass | Submission `eaae575…`; selected Claude root plus subagent; raw artifacts/unreferenced figures excluded; no Re200 rerun. |
| [x] | `workspace/codex/dsv4_flash/14` | `14` | `codex/dsv4_flash/init` at `e757c4e0…` | `codex/dsv4_flash/14` | `codex_dsv4_flash_14_80096b`; 87.5/100; DQ=false | check + check-complete pass | Submission `05261f847…`; 11 selected Codex roots (one primary plus ten continuations); all eight raw packages and report explicitly validated; no Re200 rerun. Production reconstruction blending and undisclosed M2 gradient damping deducted. |
| [ ] | `workspace/codex/generic/02` | `02` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/03` | `03` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/04` | `04` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/05` | `05` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/opus46-01` | `opus46-01` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/opus46-02` | `opus46-02` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/opus5-01` | `opus5-01` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/opus5-02` | `opus5-02` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/sonnet46-02` | `sonnet46-02` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/generic/vllm-qwen38-02` | `vllm-qwen38-02` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/gpt56/09` | `09` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/gpt56/10` | `10` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/gpt56/11` | `11` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/gpt56/12` | `12` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/kimik3/03` | `03` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/kimik3/04` | `04` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/kimik3/05` | `05` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/kimik3/07` | `07` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/codex/vllm-qwen38/03` | `03` | pre-run snapshot present | pending frozen branch check | — | — | — |
| [ ] | `workspace/oc-goal/kimik3-dsv4/05` | `05` | pre-run snapshot present | pending frozen branch check | — | — | — |

No row is complete until the immutable submission audit and both `cfdeval check`
and `cfdeval check-complete` succeed. No Re200 or other unsteady case will be
rerun.

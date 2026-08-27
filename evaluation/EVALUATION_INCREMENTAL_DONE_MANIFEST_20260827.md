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
| [x] | `workspace/codex/generic/02` | `02` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/02` | `codex_generic_02_ed5ee6`; 36/100; DQ=false | check + check-complete pass | Submission `d477eaf4…`; one Codex root plus eight descendants; explicit validator fails M0.15 metadata and Re200 horizon; no Re200 rerun. |
| [x] | `workspace/codex/generic/03` | `03` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/03` | `codex_generic_03_5f2165`; 30/100; DQ=false | check + check-complete pass | Submission `b001f2e9…`; root plus 34 Codex descendants; explicit validator fails invalid not_converged status and all production results are failed; no Re200 rerun. |
| [x] | `workspace/codex/generic/04` | `04` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/04` | `codex_generic_04_16009f`; 33/100; DQ=false | check + check-complete pass | Submission `d93a1ef1…`; selected Codex root plus three descendants; validator fails invalid equation_set and manifest calls seven cases Running; no Re200 rerun. |
| [x] | `workspace/codex/generic/05` | `05` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/05` | `codex_generic_05_2599b1`; 40/100; DQ=false | check + check-complete pass | Submission `d8491d93…`; root plus seven Codex descendants; 1,245 prohibited tracked artifacts curated index-only; all declared v3 packages fail validator metadata partitioner; no Re200 rerun. |
| [x] | `workspace/codex/generic/opus46-01` | `opus46-01` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/opus46-01` | `codex_generic_opus46-01_3a72ea`; 72/100; DQ=false | check + check-complete pass | Submission `f6796449…`; root plus 13 Codex descendants; all packages pass validator, but cylinder drag/lift physics and M2 report/status inconsistency deducted; no Re200 rerun. |
| [x] | `workspace/codex/generic/opus46-02` | `opus46-02` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/opus46-02` | `codex_generic_opus46-02_d8fed8`; 48/100; DQ=false | check + check-complete pass | Submission `1904149e…`; root plus 16 Codex descendants; individual directories validate but complete report figure manifest is broken and final M2-laminar/Re200 entries are pending; no Re200 rerun. |
| [x] | `workspace/codex/generic/opus5-01` | `opus5-01` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/opus5-01` | `codex_generic_opus5-01_83492a`; 92/100; DQ=false | check + check-complete pass | Submission `cb2bac01a…`; one Codex root plus 59 descendants; all raw packages and report explicitly validate; M2 inviscid reaches only a documented 2.55-order plateau and Re200 wake is modestly under-resolved; no Re200 rerun. |
| [x] | `workspace/codex/generic/opus5-02` | `opus5-02` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/opus5-02` | `codex_generic_opus5-02_cac77b`; 92.5/100; DQ=false | check + check-complete pass | Submission `6f43c420…`; one Codex root plus 25 descendants; all raw packages and report explicitly validate. Re200 is complete but underpredicts shedding metrics; rank-study raw inputs are excluded at the curated tip; no Re200 rerun. |
| [x] | `workspace/codex/generic/sonnet46-02` | `sonnet46-02` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/sonnet46-02` | `codex_generic_sonnet46-02_dac37b`; 80/100; DQ=true | check + check-complete pass | Submission `25daaa330…`; all packages/report validate structurally, but M0.15 is falsely marked completed/converged at 1.69 orders with unstable forces; no Re200 rerun. |
| [x] | `workspace/codex/generic/vllm-qwen38-02` | `vllm-qwen38-02` | `codex/generic/init` at `26ef1cb8…` | `codex/generic/vllm-qwen38-02` | `codex_generic_vllm-qwen38-02_d47362`; 82/100; DQ=true | check + check-complete pass | Submission `84ccf503…`; structural packages/report validate, but README documents M2 steady np8 convergence versus np4 divergence for the same production configuration (DQ4); no Re200 rerun. |
| [x] | `workspace/codex/gpt56/09` | `09` | `codex/gpt56/init` at `835bc07e…` | `codex/gpt56/09` | `codex_gpt56_09_d5760c`; 75/100; DQ=true | check + check-complete pass | Submission `7f22dbe…`; structural packages/report validate, but steady run-status files call far-below-target residual results converged (DQ7); no Re200 rerun. |
| [x] | `workspace/codex/gpt56/10` | `10` | `codex/gpt56/init` at `835bc07e…` | `codex/gpt56/10` | `codex_gpt56_10_797ac3`; 90/100; DQ=true | check + check-complete pass | Submission `02007d6f…`; one Codex root plus 47 descendants; structural packages/report validate, but all seven steady statuses call 0.02–0.67-order plateaus converged (DQ7); no Re200 rerun. |
| [x] | `workspace/codex/gpt56/11` | `11` | `codex/gpt56/init` at `835bc07e…` | `codex/gpt56/11` | `codex_gpt56_11_14a0ed`; 26/100; DQ=false | check + check-complete pass | Submission `8e2b9639…`; selected Codex root only; six canonical cases/report absent and two ignored directories lack metadata; no Re200 rerun. |
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

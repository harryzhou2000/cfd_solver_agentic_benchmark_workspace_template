# Incremental Done Evaluation Manifest — 2026-09-05

This is the current manager-side inventory of root `done` markers under
`workspace/`.  The sweep found **85** done workspaces.  **77** already map to
a canonical manager snapshot with a passing completion gate; the eight rows
below are the complete incremental evaluation queue.  The evaluator may edit
only the status and factual outcome fields below as work completes; preserve
concurrent manager changes.

All eight have an authoritative `pre_run` `.eval/env_snapshot.json`.  The
operator label is the exact final path component and must be retained
byte-for-byte in the result branch and canonical run ID.

## Pending queue

| Status | Workspace | Label | Initial branch | Initial commit | Snapshot/result branch | Notes |
|---|---|---|---|---|---|---|
| [ ] | `workspace/claude/generic/opus-11` | `opus-11` | `claude/generic/init` | `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82` | pending | Claude telemetry must be selected exclusively from workspace `.sessions/claude`. |
| [ ] | `workspace/claude/generic/sonnet-01` | `sonnet-01` | `claude/generic/init` | `ecf774c1ae14d5e9e1d11f67968bc48fdd2acf82` | pending | Claude telemetry must be selected exclusively from workspace `.sessions/claude`. |
| [ ] | `workspace/codex/generic/glm53-01` | `glm53-01` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |
| [ ] | `workspace/codex/generic/glm53-02` | `glm53-02` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |
| [ ] | `workspace/codex/generic/glm53-03` | `glm53-03` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |
| [ ] | `workspace/codex/generic/glm53-04` | `glm53-04` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |
| [ ] | `workspace/codex/generic/sonnet5-02` | `sonnet5-02` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |
| [ ] | `workspace/codex/generic/vllm-qwen38-01` | `vllm-qwen38-01` | `codex/generic/init` | `26ef1cb80b861688a4998d39431160a24ea1170a` | pending | Select the Codex root manually from workspace-local `.sessions`. |

## Calibration set

Before scoring the queue, read these completed snapshots and their scorecards,
reports, final responses, and evidence notes.  They are reference examples,
not score targets:

- High: `claude_generic_opus-04_616ae8` (99/100, no DQ) and
  `codex_kimik3_06_8208e5` (94/100, no DQ).
- Low: `codex_gpt56_11_14a0ed` (26/100, no DQ) and
  `codex_generic_03_5f2165` (30/100, no DQ).

## Completion rule

Follow `.codex/skills/cfd-benchmark-evaluation/SKILL.md` for every row:
upstream collision check without adding a workspace remote; a curated
result-branch commit at the contestant checkpoint; immutable audit and run-ID
derivation; manual workspace-local session selection; independent Code/CFD/
Results, rubric, DQ, and eight case-score layers; explicit report-PDF record;
and both `cfdeval check` and `cfdeval check-complete`.  Never rerun Re200 or
any unsteady case.  Commit manager-side snapshots only after the completion
gate passes; do not push.

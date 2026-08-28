# Incremental Done Evaluation Manifest — 2026-08-28

Scope is limited to the newly discovered root `done` workspaces below. The
exact leaf is the operator-supplied label; it must be used unchanged for the
result branch and immutable run ID. A full done-marker sweep found 75 marked
workspaces: 68 already resolve to canonical manager snapshots, and these
seven do not.

| Status | Workspace | Exact label | Frozen initial branch | Checkpoint at discovery | Canonical snapshot | Notes |
|---|---|---|---|---|---|---|
| [x] `claude_generic_opus-03_68c1ac`, 76/100, DQ=true | `workspace/claude/generic/opus-03` | `opus-03` | `claude/generic/init` | `6fb52f4a9abe` | `claude_generic_opus-03_68c1ac` | Submission `3071a743`; both gates pass. DQ7: Re200 source accepts 100x-relaxed target or max-inner exhaustion as convergence; no Re200 rerun. |
| [x] `claude_generic_opus-06_40b02c`, 92/100, DQ=false | `workspace/claude/generic/opus-06` | `opus-06` | `claude/generic/init` | `8fde7a82959a` | `claude_generic_opus-06_40b02c` | Submission `2e98f2f`; both gates pass. Strong raw workspace evidence, but immutable report rendering has missing-asset placeholders and is explicitly absent; no Re200 rerun. |
| [ ] | `workspace/claude/generic/opus-08` | `opus-08` | `claude/generic/init` | `81817257208b` | none | Root `done`; pre-run snapshot present. |
| [ ] | `workspace/codex/generic/sonnet46-01` | `sonnet46-01` | `codex/generic/init` | `8899f0006b15` | none | Root `done`; pre-run snapshot present. |
| [ ] | `workspace/codex/generic/sonnet5-01` | `sonnet5-01` | `codex/generic/init` | `f22bc25082be` | none | Root `done`; pre-run snapshot present. |
| [ ] | `workspace/codex/kimik3/06` | `06` | `codex/kimik3/init` | `7b483d5ffddec` | none | Root `done`; pre-run snapshot present. |
| [ ] | `workspace/codex/kimik3/08` | `08` | `codex/kimik3/init` | `97128af56dbe` | none | Root `done`; pre-run snapshot present. |

No row may be marked complete until its distinct contestant submission and
manager snapshot commits exist and both current completion gates pass. Re200
and all other unsteady cases are never rerun.

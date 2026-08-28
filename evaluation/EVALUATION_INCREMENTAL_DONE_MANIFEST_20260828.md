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
| [x] `claude_generic_opus-08_853afe`, 71/100, DQ=false | `workspace/claude/generic/opus-08` | `opus-08` | `claude/generic/init` | `81817257208b` | `claude_generic_opus-08_853afe` | Submission `7bf03bc`; both gates pass. All raw packages validate, but report manifest doubles `figures/` path and immutable report is only nine pages; explicit PDF absence, no Re200 rerun. |
| [x] `codex_generic_sonnet46-01_ac6940`, 73/100, DQ=false | `workspace/codex/generic/sonnet46-01` | `sonnet46-01` | `codex/generic/init` | `8899f0006b15` | `codex_generic_sonnet46-01_ac6940` | Submission `058801f`; both gates pass. 2,928 raw/build/generated artifacts curated index-only; 11-page immutable report accepted; no Re200 rerun. |
| [x] `codex_generic_sonnet5-01_540ddf`, 76/100, DQ=false | `workspace/codex/generic/sonnet5-01` | `sonnet5-01` | `codex/generic/init` | `f22bc25082be` | `codex_generic_sonnet5-01_540ddf` | Submission `4965edd`; both gates pass. Eight raw packages validate, but all-page immutable report has pervasive placeholders and PDF is explicitly absent; no Re200 rerun. |
| [x] `codex_kimik3_06_8208e5`, 94/100, DQ=false | `workspace/codex/kimik3/06` | `06` | `codex/kimik3/init` | `7b483d5ffddec` | `codex_kimik3_06_8208e5` | Submission `60a2595`; both gates pass. Eight raw packages validate; the visually reviewed 24-page sibling main report PDF is accepted as frozen manager evidence; no Re200 rerun. |
| [ ] | `workspace/codex/kimik3/08` | `08` | `codex/kimik3/init` | `97128af56dbe` | none | Root `done`; pre-run snapshot present. |

No row may be marked complete until its distinct contestant submission and
manager snapshot commits exist and both current completion gates pass. Re200
and all other unsteady cases are never rerun.

# Final Result Summary — opencode_omoslim_deepseek

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/opencode_omoslim_deepseek`
- Branch: `main` commit `abf2bf665491546f6097a45b6068714d1d27619b`
- Benchmark submodule: `a29ae3e02d8423f4e58d454d43eb34713a67a512`
- Layout: non_standard (solver: `None`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/opencode_omoslim_deepseek/cfd_solver/results`, report: `None`)
- Session window: 2026-07-04T17:53:04.401000+00:00 → 2026-07-19T10:27:09.717000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: no (run before agent started) · agent report/scores: no
- Session analysis: 2026-07-04T17:53:04.526000+00:00 → 2026-07-19T10:27:08.435000+00:00; 706×1800s buckets; idle 114 gaps / 1250851s excluded; permission-wait candidates 3; tokens 165,049,326 (cache hit 0.9679)

## Expenses

- Goal time (codex): **0 s**
- Wall time: **0 s**
- Tokens: **0** (main 0 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|

- Cost estimate: **$0.00** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /home/harry/.config/opencode)
- AGENTS.md: MISSING (question raised for user)
- CodeGraph: absent
- Benchmark submodule: a29ae3e02d84 (clean)
- opencode: v1.18.11, 1 root / 12 subagent sessions; activity 5.5h (idle 547.8h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-pro@max | max | 550000 | 4,616,480 | 4 |
| deepseek-v4-pro@medium | medium | 550000 | 63,596 | 9 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| agents_md: Which AGENTS.md was in effect for this run? The workspace has no AGENTS.md file. | AGENTS.md contents could not be recorded from the workspace | the branch's AGENTS.md or the session's world_state |  |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_0d1b` | `ses_0d1b` |  | oracle | deepseek-v4-pro@max | max | 41,090 |
| `ses_0d1b` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 31,960 |
| `ses_0d1b` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 52,589 |
| `ses_0d18` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 48,655 |
| `ses_0d18` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 43,023 |
| `ses_0d17` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 131,545 |
| `ses_0d16` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 74,441 |
| `ses_0d15` | `ses_0d1b` |  | oracle | deepseek-v4-pro@max | max | 112,350 |
| `ses_0d13` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 43,453 |
| `ses_0cf0` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 45,626 |
| `ses_0ce1` | `ses_0d1b` |  | oracle | deepseek-v4-pro@max | max | 291,846 |
| `ses_0ce1` | `ses_0d1b` |  | fixer | deepseek-v4-pro@medium | medium | 129,450 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `opencode_omoslim_deepseek/review_code.md` (overall: None)
- CFD methods review: `opencode_omoslim_deepseek/review_cfd.md` (overall: None)
- Result review: `opencode_omoslim_deepseek/review_results.md` (overall: None)

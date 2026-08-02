# Final Result Summary — omo_slim_dsv4_01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/omo_slim_dsv4_01`
- Branch: `solver/omo-slim-dsv4-01` commit `927525fa7d1bf8dbda13574c3ece7a82bf1808f7`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/omo_slim_dsv4_01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/omo_slim_dsv4_01/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/omo_slim_dsv4_01/solver/report`)
- Session window: 2026-08-01T21:45:03.554000+00:00 → 2026-08-02T08:57:26.006000+00:00

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
- AGENTS.md: sha256 618f5e015419 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 13 subagent sessions; activity 9.3h (idle 11.2h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@max | max | 550000 | 439,079 | 13 |
| deepseek-v4-pro@max | max | 550000 | 13,530,386 | 1 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_040b` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 111,136 |
| `ses_040a` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 37,316 |
| `ses_040a` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 111,459 |
| `ses_0408` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 105,666 |
| `ses_0408` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 40,775 |
| `ses_0407` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 200,927 |
| `ses_0406` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 66,139 |
| `ses_0406` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 979,650 |
| `ses_03f2` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 175,595 |
| `ses_03ef` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 90,125 |
| `ses_03ee` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 262,823 |
| `ses_03ee` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 104,838 |
| `ses_03ec` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 137,991 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_01/review_code.md` (overall: None)
- CFD methods review: `omo_slim_dsv4_01/review_cfd.md` (overall: None)
- Result review: `omo_slim_dsv4_01/review_results.md` (overall: None)

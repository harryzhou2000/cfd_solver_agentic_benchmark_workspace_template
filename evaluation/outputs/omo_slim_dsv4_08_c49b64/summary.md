# Final Result Summary — 08

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/08`
- Branch: `omo_slim/dsv4/08` commit `7d13ce3ee2792ffbad1d682c6232f7a17ea40185`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/08/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/08/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/08/solver/report`)
- Session window: 2026-08-05T08:50:23.158000+00:00 → 2026-08-06T10:07:55.309000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: accepted
- Session analysis: 2026-08-05T08:50:23.253000+00:00 → 2026-08-06T10:07:55.248000+00:00; 51×1800s buckets; idle 43 gaps / 71743s excluded; permission-wait candidates 0; tokens 147,730,902 (cache hit 0.9892)

## Expenses

- Goal time: **0 s**
- Wall time: **0 s**
- Tokens: **147,730,902** (main 7,071,958 / subagents 140,658,944)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 139,592,517 | 138,305,664 | 416,539 | 140,658,944 |
| deepseek/deepseek-v4-pro | 7,013,408 | 6,713,344 | 47,307 | 7,071,958 |

- Cost estimate: **$5.32** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/08/.sessions/opencode-config)
- AGENTS.md: sha256 15dfbb589687 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 8 subagent sessions; activity 5.9h (idle 69.5h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@max | max | ? | 677,380 | 8 |
| deepseek-v4-pro@max | max | ? | 300,064 | 1 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-flash@max: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry |
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_0297` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 529,505 |
| `ses_0298` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 5,760,695 |
| `ses_02b7` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 10,674,826 |
| `ses_02eb` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 101,086,680 |
| `ses_02ec` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 4,905,400 |
| `ses_02ed` | `ses_02ee` |  | oracle | deepseek-v4-flash@max | max | 4,107,678 |
| `ses_02ed` | `ses_02ee` |  | librarian | deepseek-v4-flash@max | max | 1,027,771 |
| `ses_02ed` | `ses_02ee` |  | fixer | deepseek-v4-flash@max | max | 12,566,389 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_08_c49b64/review_code.md` (overall: 3.85)
- CFD methods review: `omo_slim_dsv4_08_c49b64/review_cfd.md` (overall: 3.9)
- Result review: `omo_slim_dsv4_08_c49b64/review_results.md` (overall: 3.0)

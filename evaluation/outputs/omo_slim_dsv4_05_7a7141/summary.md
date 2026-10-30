# Final Result Summary — 05

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/05`
- Branch: `omo_slim/dsv4/05` commit `308ffdad1069239562786b06cac23612ac9717e4`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/05/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/05/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/05/solver/report`)
- Session window: 2026-08-04T00:40:03.680000+00:00 → 2026-08-04T15:58:30.474000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: absent
- Session analysis: 2026-08-04T10:03:52.553000+00:00 → 2026-08-04T15:58:30.057000+00:00; 12×1800s buckets; idle 1 gaps / 3959s excluded; permission-wait candidates 0; tokens 93,428,990 (cache hit 0.9858)

## Expenses

- Goal time: **0 s**
- Wall time: **0 s**
- Tokens: **93,428,990** (main 28,364,095 / subagents 65,064,895)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 41,418,483 | 40,959,744 | 156,100 | 41,843,409 |
| deepseek/deepseek-v4-pro | 51,292,322 | 50,435,712 | 194,105 | 51,585,581 |

- Cost estimate: **$5.25** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/05/.sessions/opencode-config)
- AGENTS.md: sha256 01652a033e40 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 9 subagent sessions; activity 5.4h (idle 4.7h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@default | default | ? | 21,952 | 1 |
| deepseek-v4-flash@max | max | ? | 141,243 | 5 |
| deepseek-v4-pro@max | max | ? | 519,917 | 4 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled project telemetry; no external source queried. |
| context_window_deepseek-v4-flash@max: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled project telemetry; no external source queried. |
| context_window_deepseek-v4-flash@default: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled project telemetry; no external source queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_032e` | `ses_035c` |  | ai | deepseek-v4-pro@max | max | 7,612,640 |
| `ses_032f` | `ses_035c` |  | ai | deepseek-v4-pro@max | max | 6,557,620 |
| `ses_0336` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 19,660,423 |
| `ses_0336` | `ses_035c` |  | oracle | deepseek-v4-flash@max | max | 2,134,479 |
| `ses_033a` | `ses_035c` |  | ai | deepseek-v4-pro@max | max | 9,051,226 |
| `ses_033b` | `ses_035c` |  | oracle | deepseek-v4-flash@max | max | 1,643,131 |
| `ses_033c` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 17,242,084 |
| `ses_033c` | `ses_035c` |  | explorer | deepseek-v4-flash@default | default | 451,788 |
| `ses_033c` | `ses_035c` |  | librarian | deepseek-v4-flash@max | max | 711,504 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_05_7a7141/review_code.md` (overall: 3.16)
- CFD methods review: `omo_slim_dsv4_05_7a7141/review_cfd.md` (overall: 3.42)
- Result review: `omo_slim_dsv4_05_7a7141/review_results.md` (overall: 0.5)

# Final Result Summary — 06

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/06`
- Branch: `omo_slim/dsv4/06` commit `6b84b6fffe83489ffb1463b3301963ddf392c057`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/06/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/06/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/06/solver/report`)
- Session window: 2026-08-04T00:42:38.864000+00:00 → 2026-08-05T08:27:00.978000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-04T00:42:38.990000+00:00 → 2026-08-05T08:27:00.895000+00:00; 64×1800s buckets; idle 5 gaps / 100502s excluded; permission-wait candidates 0; tokens 96,340,967 (cache hit 0.9788)

## Expenses

- Goal time: **0 s**
- Wall time: **0 s**
- Tokens: **96,340,967** (main 19,565,705 / subagents 76,775,262)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 75,976,709 | 74,573,952 | 255,727 | 76,775,262 |
| deepseek/deepseek-v4-pro | 19,467,113 | 18,849,536 | 76,020 | 19,565,705 |

- Cost estimate: **$4.38** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/06/.sessions/opencode-config)
- AGENTS.md: sha256 01652a033e40 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 15 subagent sessions; activity 4.9h (idle 46.2h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@default | default | ? | 20,261 | 2 |
| deepseek-v4-flash@max | max | ? | 770,146 | 13 |
| deepseek-v4-pro@max | max | ? | 617,577 | 1 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-flash@max: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no home, provider, or internet source was queried. |
| context_window_deepseek-v4-flash@default: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no home, provider, or internet source was queried. |
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no home, provider, or internet source was queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_02ef` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 3,623,796 |
| `ses_031b` | `ses_035c` |  | oracle | deepseek-v4-flash@max | max | 1,893,480 |
| `ses_031b` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 581,849 |
| `ses_033c` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 49,383,571 |
| `ses_0358` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 2,074,717 |
| `ses_0359` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 657,053 |
| `ses_0359` | `ses_035c` |  | oracle | deepseek-v4-flash@max | max | 490,974 |
| `ses_0359` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 1,657,280 |
| `ses_035a` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 2,384,004 |
| `ses_035a` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 5,290,646 |
| `ses_035a` | `ses_035c` |  | oracle | deepseek-v4-flash@max | max | 212,381 |
| `ses_035c` | `ses_035c` |  | fixer | deepseek-v4-flash@max | max | 7,236,041 |
| `ses_035c` | `ses_035c` |  | explorer | deepseek-v4-flash@default | default | 26,499 |
| `ses_035c` | `ses_035c` |  | explorer | deepseek-v4-flash@default | default | 231,523 |
| `ses_035c` | `ses_035c` |  | librarian | deepseek-v4-flash@max | max | 1,031,448 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_06_d8d434/review_code.md` (overall: 3.0)
- CFD methods review: `omo_slim_dsv4_06_d8d434/review_cfd.md` (overall: 3.0)
- Result review: `omo_slim_dsv4_06_d8d434/review_results.md` (overall: 2.8)

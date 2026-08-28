# Final Result Summary — 01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/01`
- Branch: `omo_slim/dsv4/01` commit `51d1a785f4a25b3cff7e79294459ed22579addb7`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/01/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/01/solver/report`)
- Session window: 2026-08-01T21:45:03.554000+00:00 → 2026-08-02T08:57:26.006000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: accepted
- Session analysis: 2026-08-01T21:45:03.647000+00:00 → 2026-08-02T08:57:25.674000+00:00; 23×1800s buckets; idle 9 gaps / 10786s excluded; permission-wait candidates 0; tokens 164,977,906 (cache hit 0.91)

## Expenses

- Goal time: **0 s**
- Wall time: **0 s**
- Tokens: **164,977,906** (main 15,269,370 / subagents 149,708,536)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 148,483,861 | 147,284,096 | 447,910 | 149,708,536 |
| deepseek/deepseek-v4-pro | 15,176,082 | 1,645,696 | 68,388 | 15,269,370 |

- Cost estimate: **$12.80** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/01/.sessions/opencode-config)
- AGENTS.md: sha256 618f5e015419 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 13 subagent sessions; activity 9.3h (idle 11.2h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@max | max | ? | 439,079 | 13 |
| deepseek-v4-pro@max | max | ? | 13,530,386 | 1 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-flash@max: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no local model catalog entry is present and no home, provider, or internet source was queried. |
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no local model catalog entry is present and no home, provider, or internet source was queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_03ec` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 5,176,071 |
| `ses_03ee` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 1,437,062 |
| `ses_03ee` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 15,488,807 |
| `ses_03ef` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 718,349 |
| `ses_03f2` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 4,692,459 |
| `ses_0406` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 98,748,226 |
| `ses_0406` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 560,347 |
| `ses_0407` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 10,881,119 |
| `ses_0408` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 149,063 |
| `ses_0408` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 3,211,586 |
| `ses_040a` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 4,034,915 |
| `ses_040a` | `ses_040b` |  | oracle | deepseek-v4-flash@max | max | 97,348 |
| `ses_040b` | `ses_040b` |  | fixer | deepseek-v4-flash@max | max | 4,513,184 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_01_12c8d9/review_code.md` (overall: 4.2)
- CFD methods review: `omo_slim_dsv4_01_12c8d9/review_cfd.md` (overall: 4.53)
- Result review: `omo_slim_dsv4_01_12c8d9/review_results.md` (overall: 4.45)

# Final Result Summary — 02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/02`
- Branch: `omo_slim/dsv4/02` commit `e272cd3f195e4d7cf490acd9be3578c7893056e0`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/02/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/02/solver/report`)
- Session window: 2026-08-02T08:24:33.589000+00:00 → 2026-08-02T10:18:34.601000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-02T08:24:33.667000+00:00 → 2026-08-02T10:18:34.345000+00:00; 4×1800s buckets; idle 1 gaps / 2685s excluded; permission-wait candidates 0; tokens 21,231,664 (cache hit 0.0679)

## Expenses

- Goal time (codex): **0 s**
- Wall time: **0 s**
- Tokens: **21,231,664** (main 21,231,664 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-pro | 21,143,002 | 1,435,264 | 67,379 | 21,231,664 |

- Cost estimate: **$11.27** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/02/.sessions/opencode-config)
- AGENTS.md: sha256 618f5e015419 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 0 subagent sessions; activity 1.2h (idle 0.7h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-pro@max | max | ? | 19,707,738 | 1 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no home, provider, or internet source was queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_02_b52628/review_code.md` (overall: 2.3)
- CFD methods review: `omo_slim_dsv4_02_b52628/review_cfd.md` (overall: 2.14)
- Result review: `omo_slim_dsv4_02_b52628/review_results.md` (overall: 1.45)

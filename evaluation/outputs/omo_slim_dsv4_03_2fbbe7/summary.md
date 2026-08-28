# Final Result Summary — 03

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/03`
- Branch: `omo_slim/dsv4/03` commit `72c27c36c8d01db85d8730cda1373cdb46f44751`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/03/solver`, results: `None`, report: `None`)
- Session window: 2026-08-03T13:59:46.043000+00:00 → 2026-08-04T00:36:22.073000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: absent
- Session analysis: 2026-08-03T14:01:42.685000+00:00 → 2026-08-04T00:36:21.788000+00:00; 22×1800s buckets; idle 1 gaps / 34685s excluded; permission-wait candidates 0; tokens 27,743,062 (cache hit 0.9638)

## Expenses

- Goal time: **0 s**
- Wall time: **0 s**
- Tokens: **27,743,062** (main 27,649,219 / subagents 93,843)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 88,561 | 73,216 | 3,479 | 93,843 |
| deepseek/deepseek-v4-pro | 27,536,139 | 26,552,448 | 86,511 | 27,649,219 |

- Cost estimate: **$2.24** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools: 
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.11 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/omo-slim/dsv4/03/.sessions/opencode-config)
- AGENTS.md: sha256 01652a033e40 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.11, 1 root / 1 subagent sessions; activity 0.8h (idle 9.6h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| deepseek-v4-flash@default | default | ? | 15,345 | 1 |
| deepseek-v4-pro@max | max | ? | 983,691 | 1 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_deepseek-v4-flash@default: What is the context window (max tokens) of model `deepseek-v4-flash`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in the bundled immutable telemetry; no home, provider, or internet source was queried. |
| context_window_deepseek-v4-pro@max: What is the context window (max tokens) of model `deepseek-v4-pro`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in the bundled immutable telemetry; no home, provider, or internet source was queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_0381` | `ses_0381` |  | explorer | deepseek-v4-flash@default | default | 93,843 |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `omo_slim_dsv4_03_2fbbe7/review_code.md` (overall: 2.45)
- CFD methods review: `omo_slim_dsv4_03_2fbbe7/review_cfd.md` (overall: 1.75)
- Result review: `omo_slim_dsv4_03_2fbbe7/review_results.md` (overall: 0.0)

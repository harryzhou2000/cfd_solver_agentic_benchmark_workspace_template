# Final Result Summary — 01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/oc-goal/gpt56-dsv4/01`
- Branch: `oc-goal/gpt56-dsv4/01` commit `908a5234f8bf7647bb4f39d9eb0316d1118bf18d`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/oc-goal/gpt56-dsv4/01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/oc-goal/gpt56-dsv4/01/solver/.probe_implicit_pseudotransient_bridge_m080_np8_20260808`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/oc-goal/gpt56-dsv4/01/solver/report`)
- Session window: 2026-08-06T20:05:43.295000+00:00 → 2026-08-10T07:01:44.847000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Session analysis: 2026-08-06T20:05:43.531000+00:00 → 2026-08-10T07:01:39.976000+00:00; 166×1800s buckets; idle 75 gaps / 228712s excluded; permission-wait candidates 1; tokens 430,551,708 (cache hit 0.9485)

## Expenses

- Goal time (codex): **0 s**
- Wall time: **0 s**
- Tokens: **430,551,708** (main 20,472,279 / subagents 410,079,429)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| deepseek/deepseek-v4-flash | 7,202,790 | 6,999,680 | 29,444 | 7,276,005 |
| internal_openai_eccn/us/azure/openai/eccn-gpt-5.6-sol | 421,488,989 | 399,617,531 | 1,053,540 | 423,275,703 |
| minimax/MiniMax-M3 | 0 | 0 | 0 | 0 |

- Cost estimate: **$316.03** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **0**; top tools:
- Subagent spawns: 0
- LOC (file scan): 0 lines / 0 files

## Metadata

- Harness: opencode v1.18.14 (config: /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/oc-goal/gpt56-dsv4/01/.sessions/opencode-config)
- AGENTS.md: sha256 6f67a229e3d8 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)
- opencode: v1.18.14, 1 root / 38 subagent sessions; activity 23.1h (idle 148.3h excluded, gap threshold 600s)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| MiniMax-M3@default | default | 550000 | n/a | 1 |
| deepseek-v4-flash@max | max | 550000 | 138,453 | 2 |
| us/azure/openai/eccn-gpt-5.6-sol@high | high | ? | 4,368 | 36 |

### Metadata questions for user (unextractable fields)

| Question | Reason | Suggested source | Answer |
|----------|--------|------------------|--------|
| context_window_us_azure_openai_eccn-gpt-5.6-sol@high: What is the context window (max tokens) of model `us/azure/openai/eccn-gpt-5.6-sol`? | model is not listed in the local model catalog | provider docs or model card | Unavailable in bundled immutable telemetry; no local model catalog entry is present and no home, provider, or internet source was queried. |
Provide answers as `{"<question_id>": "..."}` and re-run with `--answers <file>`; status then flips to complete.

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `ses_0275` | `ses_0275` |  | general | us/azure/openai/eccn-gpt-5.6-sol@high | high | 3,300,679 |
| `ses_0275` | `ses_0275` |  | architect | us/azure/openai/eccn-gpt-5.6-sol@high | high | 162,142 |
| `ses_0275` | `ses_0275` |  | documentation | us/azure/openai/eccn-gpt-5.6-sol@high | high | 173,784 |
| `ses_0274` | `ses_0275` |  | general | us/azure/openai/eccn-gpt-5.6-sol@high | high | 4,517,402 |
| `ses_0273` | `ses_0275` |  | architect | us/azure/openai/eccn-gpt-5.6-sol@high | high | 1,437,373 |
| `ses_0273` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 198,271,219 |
| `ses_0272` | `ses_0275` |  | architect | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,384,438 |
| `ses_0272` | `ses_0275` |  | code | us/azure/openai/eccn-gpt-5.6-sol@high | high | 1,527,067 |
| `ses_0270` | `ses_0275` |  | cli | us/azure/openai/eccn-gpt-5.6-sol@high | high | 17,721,867 |
| `ses_0270` | `ses_0275` |  | tooling | us/azure/openai/eccn-gpt-5.6-sol@high | high | 12,179,698 |
| `ses_0270` | `ses_0275` |  | code | us/azure/openai/eccn-gpt-5.6-sol@high | high | 8,078,521 |
| `ses_0270` | `ses_0275` |  | architect | us/azure/openai/eccn-gpt-5.6-sol@high | high | 8,376,768 |
| `ses_026d` | `ses_0275` |  | performance | us/azure/openai/eccn-gpt-5.6-sol@high | high | 985,733 |
| `ses_0261` | `ses_0275` |  | debugger | us/azure/openai/eccn-gpt-5.6-sol@high | high | 892,376 |
| `ses_0261` | `ses_0275` |  | architect | us/azure/openai/eccn-gpt-5.6-sol@high | high | 1,704,321 |
| `ses_0261` | `ses_0275` |  | data | us/azure/openai/eccn-gpt-5.6-sol@high | high | 1,751,918 |
| `ses_0260` | `ses_0275` |  | code | us/azure/openai/eccn-gpt-5.6-sol@high | high | 912,034 |
| `ses_0260` | `ses_0275` |  | debugger | us/azure/openai/eccn-gpt-5.6-sol@high | high | 1,103,544 |
| `ses_022c` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 58,190,160 |
| `ses_01e2` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 12,478,816 |
| `ses_01ba` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,916,978 |
| `ses_01ba` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 5,221,405 |
| `ses_01ad` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 16,875,530 |
| `ses_018f` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,440,500 |
| `ses_017f` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,008,931 |
| `ses_017d` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,837,248 |
| `ses_0178` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 10,950,051 |
| `ses_0177` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,341,481 |
| `ses_0176` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 4,699,804 |
| `ses_0161` | `ses_0275` |  | backend | us/azure/openai/eccn-gpt-5.6-sol@high | high | 2,479,679 |
| ... | 8 more | | | | | |

### Prompts


### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `oc-goal_gpt56-dsv4_01_36a01a/review_code.md` (overall: 4.2)
- CFD methods review: `oc-goal_gpt56-dsv4_01_36a01a/review_cfd.md` (overall: 4.73)
- Result review: `oc-goal_gpt56-dsv4_01_36a01a/review_results.md` (overall: 4.5)

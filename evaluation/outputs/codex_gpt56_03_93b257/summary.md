# Final Result Summary — 03

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/03`
- Branch: `codex/gpt56/03` commit `f06ea3fa63acbdbfce71ecd4debf603c355f751a`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/03/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/03/solver/debug_outputs`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/03/solver/report`)
- Session window: 2026-08-03T23:46:00.103000+00:00 → 2026-08-04T05:41:33.774000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: absent
- Session analysis: 2026-08-03T23:46:00.103000+00:00 → 2026-08-04T05:41:33.774000+00:00; 12×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 128,560,924 (cache hit 0.971)

## Expenses

- Goal time: **21305 s**
- Wall time: **21334 s**
- Tokens: **128,560,924** (main 116,122,538 / subagents 12,438,386)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fca03` | complete | gpt-5.6-sol | 4 | 128,560,924 | 21305 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-sol | 115,865,273 | 113,713,408 | 257,265 | 116,122,538 |
| gpt-5.6-terra | 12,333,016 | 10,770,176 | 105,370 | 12,438,386 |

- Cost estimate: **$82.93** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **918**; top tools: exec=693, wait=200, followup_task=10, list_agents=6, send_message=4, spawn_agent=3
- Subagent spawns: 3
- LOC (file scan): 4,962 lines / 20 files
- LOC (git tracked): 4,962 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai_vanilla)
- AGENTS.md: sha256 8c9f03983ec1 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-sol | max | 258400 | 245,770 | 1 |
| gpt-5.6-terra | high, xhigh | 258400 | 1,757,711 | 3 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fca06` | `019fca03` | Parfit | requirements_audit | gpt-5.6-terra | high | 4,071,694 |
| `019fca06` | `019fca03` | Carver | environment_mesh_audit | gpt-5.6-terra | high | 3,479,938 |
| `019fca06` | `019fca03` | Gibbs | architecture_review | gpt-5.6-terra | xhigh | 4,886,754 |

### Prompts

- `019fca03` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `codex_gpt56_03_93b257/review_code.md` (overall: 4.85)
- CFD methods review: `codex_gpt56_03_93b257/review_cfd.md` (overall: 4.92)
- Result review: `codex_gpt56_03_93b257/review_results.md` (overall: 4.95)

# Final Result Summary — 07

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/07`
- Branch: `codex/gpt56/07` commit `7ad799097f6d8426eb34d5a5ba092956a3e7d7b7`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/07/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/07/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/07/solver/report`)
- Session window: 2026-08-07T11:15:02.806000+00:00 → 2026-08-09T08:56:12.800000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-07T11:15:02.806000+00:00 → 2026-08-09T08:56:12.800000+00:00; 92×1800s buckets; idle 1 gaps / 136442s excluded; permission-wait candidates 0; tokens 179,288,436 (cache hit 0.9834)

## Expenses

- Goal time: **690 s**
- Wall time: **164470 s**
- Tokens: **179,288,436** (main 179,288,436 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fdbed` | complete | gpt-5.5 | 1 | 179,288,436 | 690 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.5 | 178,927,179 | 175,953,920 | 361,257 | 179,288,436 |

- Cost estimate: **$60.12** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **1,238**; top tools: exec_command=631, write_stdin=425, apply_patch=131, update_plan=33, codegraph_explore=14, get_goal=2
- Subagent spawns: 0
- LOC (file scan): 10,456 lines / 46 files
- LOC (git tracked): 4,573 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai_vanilla)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 0a05c72c06d3 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.5 | xhigh | 272000 | 11,241,129,231 | 1 |

### Prompts

- `019fdbed` goal: complete the task defined in cfd_solver_agentic_benchmark/ . Latex is provided.
  - initial: /goal complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal complete the task defined in cfd_solver_agentic_benchmark/ . Latex is provided.

### Rule-violation candidates

None detected.

## Reviews

- Code review scorecard: `codex_gpt56_07_b7c2c8/review_code.md` (overall: 3.7)
- CFD methods review: `codex_gpt56_07_b7c2c8/review_cfd.md` (overall: 3.6)
- Result review: `codex_gpt56_07_b7c2c8/review_results.md` (overall: 2.4)

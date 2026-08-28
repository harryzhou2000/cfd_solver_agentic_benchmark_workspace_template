# Final Result Summary — opus-09

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09`
- Branch: `claude/generic/opus-09` commit `7e16370db4546b8e957a612177f850742bf4b6f8`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-09/solver/report`)
- Session window: 2026-08-28T01:41:57.259000+00:00 → 2026-08-28T04:55:22.374000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-28T01:41:57.259000+00:00 → 2026-08-28T04:55:22.374000+00:00; 7×1800s buckets; idle 1 gaps / 603s excluded; permission-wait candidates 0; tokens 50,513,895 (cache hit 0.9363)

## Expenses

- Goal time: **unavailable**
- Wall time: **11605 s**
- Tokens: **50,513,895** (main 49,336,233 / subagents 1,177,662)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `cfdc5453` | None | None | 6 | 50,513,895 | None |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| claude-opus-4-6 | 50,309,091 | 47,103,937 | 204,804 | 50,513,895 |

- Cost estimate: **$48.70** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **335**; top tools: Bash=165, Edit=65, Read=55, Write=19, TaskUpdate=12, TaskCreate=9
- Subagent spawns: 5
- LOC (file scan): 3,888 lines / 16 files
- LOC (git tracked): 3,888 lines

## Metadata

- Harness: claude cli 2.1.241 (None, provider None)
- AGENTS.md: sha256 2c0ded94ff6e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| claude-opus-4-6 | high | ? | n/a | 6 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `cfdc5453` | `cfdc5453` |  | claude_subagent | claude-opus-4-6 | high | 377,343 |
| `cfdc5453` | `cfdc5453` |  | claude_subagent | claude-opus-4-6 | high | 498,922 |
| `cfdc5453` | `cfdc5453` |  | claude_subagent | claude-opus-4-6 | high | 97,269 |
| `cfdc5453` | `cfdc5453` |  | claude_subagent | claude-opus-4-6 | high | 114,024 |
| `cfdc5453` | `cfdc5453` |  | claude_subagent | claude-opus-4-6 | high | 90,104 |

### Prompts

- `cfdc5453` goal: none
  - initial: <command-name>/goal</command-name>
            <command-message>goal</command-message>
            <command-args>Complete the task defined in @cfd_solver_agentic_benchmark/ .</command-args>
  - resume: <local-command-stdout>Goal set: Complete the task defined in @cfd_solver_agentic_benchmark/ .</local-command-stdout>

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `cfdc5453` | {"command": "git checkout -b solver/cfd-benchmark-attempt", "description": "Create new branch for solver work"} |
| medium | network_access | `cfdc5453` | nd": "python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib 2>&1 | tail -5", "description": "Set up Python virtual environment", "timeout": 120000} |
| medium | suspicious_patterns | `cfdc5453` | {"command": "pkill -9 cfd2d 2>/dev/null; killall -9 mpirun 2>/dev/null; sleep 1; echo \"killed\"", "description": "Kill running processes"} |

## Reviews

- Code review scorecard: `claude_generic_opus-09_9cb2fc/review_code.md` (overall: 4.0)
- CFD methods review: `claude_generic_opus-09_9cb2fc/review_cfd.md` (overall: 4.0)
- Result review: `claude_generic_opus-09_9cb2fc/review_results.md` (overall: 4.0)

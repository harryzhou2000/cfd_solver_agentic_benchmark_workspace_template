# Final Result Summary — opus-07

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-07`
- Branch: `claude/generic/opus-07` commit `eaae575d90bfb39a57702e0fcb8dc5e98163568f`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-07/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-07/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-07/solver/report`)
- Session window: 2026-08-27T04:14:45.523000+00:00 → 2026-08-27T07:30:48.544000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-27T04:14:45.523000+00:00 → 2026-08-27T07:30:48.544000+00:00; 7×1800s buckets; idle 1 gaps / 780s excluded; permission-wait candidates 0; tokens 183,309,524 (cache hit 0.9959)

## Expenses

- Goal time: **unavailable**
- Wall time: **11763 s**
- Tokens: **183,309,524** (main 182,385,758 / subagents 923,766)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `1ea2a29d` | None | None | 2 | 183,309,524 | None |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| claude-opus-4-6 | 182,987,802 | 182,235,904 | 321,722 | 183,309,524 |

- Cost estimate: **unavailable** (unpriced tokens: 183,309,524)

## Measurements

- Tool calls: **592**; top tools: Bash=475, Edit=39, Read=30, Write=19, Monitor=13, TaskUpdate=9
- Subagent spawns: 1
- LOC (file scan): 3,684 lines / 13 files
- LOC (git tracked): 3,684 lines

## Metadata

- Harness: claude cli 2.1.241 (None, provider None)
- AGENTS.md: sha256 2c0ded94ff6e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| claude-opus-4-6 | high | ? | n/a | 2 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `1ea2a29d` | `1ea2a29d` |  | claude_subagent | claude-opus-4-6 | high | 923,766 |

### Prompts

- `1ea2a29d` goal: none
  - initial: <command-name>/model</command-name>
            <command-message>model</command-message>
            <command-args>claude-opus-4-6[1m]</command-args>
  - resume: <local-command-stdout>Set model to [1mOpus 4.6 (1M context)[22m and saved as your default for new sessions</local-command-stdout>
  - resume: <command-name>/goal</command-name>
            <command-message>goal</command-message>
            <command-args>Complete the task defined in @cfd_solver_agentic_benchmark/ .</command-args>
  - resume: <local-command-stdout>Goal set: Complete the task defined in @cfd_solver_agentic_benchmark/ .</local-command-stdout>
  - resume: <task-notification>
<task-id>b1kf7rn58</task-id>
<summary>Monitor event: "Wait for Re20 VTU"</summary>
<event>[Monitor timed out — re-arm if needed.]</event>
</task-notification>

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `1ea2a29d` | {"command": "git checkout -b solver/cfd2d 2>&1", "description": "Create new branch for solver work"} |
| medium | network_access | `1ea2a29d` | er && python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib 2>&1 | tail -5", "description": "Create Python venv and install dependencies", "timeout": 60000} |
| medium | suspicious_patterns | `1ea2a29d` | {"command": "# Kill all solver processes\npkill -9 -f \"cfd2d\" 2>/dev/null\nsleep 3\necho \"All killed\"\nps aux | grep \"[c]fd2d\" | wc -l", "description": "Kill all sol |
| medium | suspicious_patterns | `1ea2a29d` | {"command": "pkill -9 -f \"mpirun\" 2>/dev/null; sleep 3; ps aux | grep \"[c]fd2d\\|[m]pirun\" | wc -l", "description": "Kill mpirun too"} |
| medium | suspicious_patterns | `1ea2a29d` | ux | grep \"[c]fd2d\\|[m]pirun\" | awk '{print $2}' | xargs kill -9 2>/dev/null; sleep 3\nps aux | grep \"[c]fd2d\\|[m]pirun\" | grep -v grep | wc -l", "description": "Force kill remaining |
| medium | suspicious_patterns | `1ea2a29d` | {"command": "# Kill the hung M080 laminar processes\npkill -9 -f \"naca0012_m080_laminar_re5000\" 2>/dev/null; sleep 2\nps aux | grep \"cfd2d\" | grep -v grep | wc -l\necho \"remaini |

## Reviews

- Code review scorecard: `claude_generic_opus-07_c08c67/review_code.md` (overall: 4.0)
- CFD methods review: `claude_generic_opus-07_c08c67/review_cfd.md` (overall: 4.0)
- Result review: `claude_generic_opus-07_c08c67/review_results.md` (overall: 4.0)

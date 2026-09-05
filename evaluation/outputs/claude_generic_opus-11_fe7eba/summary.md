# Final Result Summary — opus-11

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-11`
- Branch: `claude/generic/opus-11` commit `9965be84b59114b4fb573a9253f9e14e24edfa94`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-11/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-11/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-11/solver/report`)
- Session window: 2026-08-28T05:24:49.538000+00:00 → 2026-08-28T09:06:58.404000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-28T05:24:49.538000+00:00 → 2026-08-28T09:06:58.404000+00:00; 8×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 154,608,678 (cache hit 0.9939)

## Expenses

- Goal time: **unavailable**
- Wall time: **13329 s**
- Tokens: **154,608,678** (main 152,460,286 / subagents 2,148,392)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `87209b46` | None | None | 6 | 154,608,678 | None |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| claude-opus-4-8 | 154,041,290 | 153,098,399 | 567,388 | 154,608,678 |

- Cost estimate: **$96.63** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **466**; top tools: Bash=261, Edit=80, Read=74, Write=33, Monitor=11, Agent=5
- Subagent spawns: 5
- LOC (file scan): 4,140 lines / 25 files
- LOC (git tracked): 4,076 lines

## Metadata

- Harness: claude cli 2.1.241 (None, provider None)
- AGENTS.md: sha256 2c0ded94ff6e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| claude-opus-4-8 | high | ? | n/a | 6 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `87209b46` | `87209b46` |  | claude_subagent | claude-opus-4-8 | high | 591,344 |
| `87209b46` | `87209b46` |  | claude_subagent | claude-opus-4-8 | high | 455,213 |
| `87209b46` | `87209b46` |  | claude_subagent | claude-opus-4-8 | high | 238,587 |
| `87209b46` | `87209b46` |  | claude_subagent | claude-opus-4-8 | high | 282,827 |
| `87209b46` | `87209b46` |  | claude_subagent | claude-opus-4-8 | high | 580,421 |

### Prompts

- `87209b46` goal: none
  - initial: <command-name>/goal</command-name>
            <command-message>goal</command-message>
            <command-args>Complete the task defined in @cfd_solver_agentic_benchmark/ .</command-args>
  - resume: <local-command-stdout>Goal set: Complete the task defined in @cfd_solver_agentic_benchmark/ .</local-command-stdout>

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `87209b46` | {"command": "git checkout -b solver/cfd-attempt 2>&1 && git branch --show-current", "description": "Create solver work branch"} |
| medium | network_access | `87209b46` | e/solver\npython3 -m venv .venv 2>&1 | tail -2\n./.venv/bin/pip install --quiet --disable-pip-version-check numpy matplotlib scipy 2>&1 | tail -5\n./.venv/bin/python -c \"import numpy,matplot |
| medium | suspicious_patterns | `87209b46` | {"command": "pkill -9 -f m200_inviscid 2>/dev/null; ps aux | grep '[u]ns2d' | grep -oE 'case [^ ]+' | sort | uniq -c; echo \"done\"", "descrip |
| medium | suspicious_patterns | `87209b46` | {"command": "pkill -9 -f cylinder_m010_laminar_re20 2>/dev/null; echo \"killed cyl\"; sleep 1", "description": "Kill cylinder run"} |
| medium | suspicious_patterns | `87209b46` | {"command": "cd /workspace/solver\npkill -9 -f 'out_cyllong\\|out_cylcap\\|cyl12k' 2>/dev/null\nsleep 2\nps aux | grep '[u]ns2d' | grep -oE 'case [^ ]+' | sort | un |
| medium | suspicious_patterns | `87209b46` | {"command": "cd /workspace/solver\npkill -9 -f 'laminar_re20' 2>/dev/null; sleep 2\necho \"Re20 procs left: $(ps aux|grep -c '[l]aminar_re20')\"\nexport LD_LIBRARY_ |
| medium | suspicious_patterns | `87209b46` | {"command": "cd /workspace/solver\nkill -9 $(ps aux|grep '[o]ut_cylF30'|awk '{print $2}') 2>/dev/null; sleep 1; echo \"killed cylF30\"", "description": "Kill CFL-1 |
| medium | suspicious_patterns | `87209b46` | {"command": "cd /workspace/solver\npkill -9 -f cyl10kt 2>/dev/null; sleep 1\nexport LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH\n |
| medium | suspicious_patterns | `87209b46` | rep '[u]ns2d' | grep 'scratch/out_' | awk '{print $2}'); do kill -9 $pid 2>/dev/null; done\nsleep 1\necho \"remaining uns2d by target:\"\nps aux | grep '[u]ns2d' | grep -oE 'output [^ ]+'  |
| medium | suspicious_patterns | `87209b46` | ep '[u]ns2d' | grep 'laminar_re200' | awk '{print $2}'); do kill -9 $pid 2>/dev/null; done\nsleep 2; echo \"uns2d running: $(ps aux|grep -c '[u]ns2d')\"", "description": "Check and kill sl |
| medium | suspicious_patterns | `87209b46` | /workspace/solver\nfor pid in $(pgrep -f 'build/uns2d'); do kill -9 $pid 2>/dev/null; done\nsleep 2; echo \"uns2d now: $(pgrep -fc 'build/uns2d' || echo 0)\"", "description": "Kill all rem |
| medium | suspicious_patterns | `87209b46` | /workspace/solver\nfor pid in $(pgrep -f 'build/uns2d'); do kill -9 $pid 2>/dev/null; done\nsleep 2; echo \"killed all; remaining: $(pgrep -fc 'build/uns2d' || echo 0)\"\nexport CFD_EXTERN |
| medium | suspicious_patterns | `87209b46` | /workspace/solver\nfor pid in $(pgrep -f 'build/uns2d'); do kill -9 $pid 2>/dev/null; done\nsleep 2\nexport CFD_EXTERNALS_ROOT=/workspace/external/cfd_externals/install\nexport LD_LIBRARY_ |
| medium | suspicious_patterns | `87209b46` | /workspace/solver; for pid in $(pgrep -f 'build/uns2d'); do kill -9 $pid 2>/dev/null; done; sleep 2; echo \"clean: $(pgrep -fc 'build/uns2d' || echo 0) procs\"; rm -f results/group_naca3.l |

## Reviews

- Code review scorecard: `claude_generic_opus-11_fe7eba/review_code.md` (overall: 4.0)
- CFD methods review: `claude_generic_opus-11_fe7eba/review_cfd.md` (overall: 4.0)
- Result review: `claude_generic_opus-11_fe7eba/review_results.md` (overall: 4.0)

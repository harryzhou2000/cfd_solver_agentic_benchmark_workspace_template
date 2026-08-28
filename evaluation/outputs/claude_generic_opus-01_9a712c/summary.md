# Final Result Summary — opus-01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-01`
- Branch: `claude/generic/opus-01` commit `8dae7ba6b828c8b72dfa9e55e407f6c4e8098303`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-01/solver/results/_rank_study`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/claude/generic/opus-01/solver/report`)
- Session window: 2026-08-25T12:24:12.933000+00:00 → 2026-08-25T17:40:38.570000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-25T12:24:12.933000+00:00 → 2026-08-25T17:40:38.570000+00:00; 11×1800s buckets; idle 3 gaps / 5654s excluded; permission-wait candidates 0; tokens 701,056,834 (cache hit 0.9942)

## Expenses

- Goal time: **unavailable**
- Wall time: **18986 s**
- Tokens: **701,056,834** (main 682,026,442 / subagents 19,030,392)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `3a8e926a` | None | None | 6 | 701,056,834 | None |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| claude-opus-5 | 700,298,872 | 696,241,574 | 757,962 | 701,056,834 |

- Cost estimate: **unavailable** (unpriced tokens: 701,056,834)

## Measurements

- Tool calls: **1,333**; top tools: Bash=1295, Read=13, Edit=8, Agent=5, TaskStop=4, Monitor=3
- Subagent spawns: 5
- LOC (file scan): 8,343 lines / 46 files
- LOC (git tracked): 8,343 lines

## Metadata

- Harness: claude cli 2.1.241 (None, provider None)
- AGENTS.md: sha256 2c0ded94ff6e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| claude-opus-5 | high | ? | n/a | 6 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `3a8e926a` | `3a8e926a` |  | claude_subagent | claude-opus-5 | high | 3,229,748 |
| `3a8e926a` | `3a8e926a` |  | claude_subagent | claude-opus-5 | high | 7,035,414 |
| `3a8e926a` | `3a8e926a` |  | claude_subagent | claude-opus-5 | high | 1,866,699 |
| `3a8e926a` | `3a8e926a` |  | claude_subagent | claude-opus-5 | high | 4,759,341 |
| `3a8e926a` | `3a8e926a` |  | claude_subagent | claude-opus-5 | high | 2,139,190 |

### Prompts

- `3a8e926a` goal: none
  - initial: <command-name>/goal</command-name>
            <command-message>goal</command-message>
            <command-args>Complete the task defined in @cfd_solver_agentic_benchmark/ .</command-args>
  - resume: <local-command-stdout>Goal set: Complete the task defined in @cfd_solver_agentic_benchmark/ .</local-command-stdout>
  - resume: [Request interrupted by user for tool use]
  - resume: <task-notification>
<task-id>bwrp60rid</task-id>
<tool-use-id>toolu_017oa7rfDAe1FGtMWV8gqXSU</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/bwrp60rid
  - resume: [Request interrupted by user for tool use]
  - resume: <task-notification>
<task-id>bvhzeflsy</task-id>
<tool-use-id>toolu_01UHRBMqyaxnVCrRdCJSqgpP</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/bvhzeflsy
  - resume: <task-notification>
<task-id>bl2xczbq6</task-id>
<tool-use-id>toolu_0161Cw3EjzCZyLA5tzy3LJc5</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/bl2xczbq6
  - resume: <task-notification>
<task-id>b0g4q54iu</task-id>
<tool-use-id>toolu_01K4959mbuKxAQRnyoxmNkaA</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/b0g4q54iu
  - resume: <task-notification>
<task-id>b0db114lm</task-id>
<tool-use-id>toolu_01AdojB8tVRixSH5GhakqkTP</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/b0db114lm
  - resume: <task-notification>
<task-id>bu7zwweu1</task-id>
<tool-use-id>toolu_01TNPZbs9Vp4JcL53KaNS4cM</tool-use-id>
<output-file>/tmp/claude-1004/-workspace/3a8e926a-3e32-45e8-a8dd-893091c1e00f/tasks/bu7zwweu1
  - resume: <task-notification>
<task-id>b7lulxnzu</task-id>
<summary>Monitor event: "Re200 transient progress every 2500 steps"</summary>
<event>[Monitor timed out — re-arm if needed.]</event>
</task-notificatio
  - resume: <task-notification>
<summary>Goal check-in: background work still running</summary>
</task-notification>
<system-reminder>
Goal check-in: «Complete the task defined in @cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `3a8e926a` | thon3 -m venv .venv 2>&1 | tail -2 && timeout 300 .venv/bin/pip install --quiet numpy matplotlib h5py scipy 2>&1 | tail -5; .venv/bin/python -c \"import numpy,matplotlib,h5py,scipy;print('ok' |
| medium | network_access | `3a8e926a` | <workspace>\npython3 -m venv solver/.venv\nsolver/.venv/bin/pip install numpy matplotlib h5py scipy\n\n# 1. all eight cases at np=8 (writes solver/report/run_manifest.csv)\nsolver/.venv/bin/p |
| medium | suspicious_patterns | `3a8e926a` | {"command": "kill -9 19772 2>/dev/null; sleep 1; pgrep -f chain_steady || echo \"chain disarmed\"", "description": "Force kill chain"} |
| medium | suspicious_patterns | `3a8e926a` | nd": "ps aux | grep \"[_]k2\" | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 2; ps aux | grep \"[f]vm2d solve\" | grep -o \"output [^ ]*\" | sort -u; echo \"cleaned\"", "descript |
| medium | suspicious_patterns | `3a8e926a` | nd": "ps aux | grep \"[_]k3\" | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 1\npython3 - <<'PY'\np='src/core/CaseConfig.h'; s=open(p).read()\ns=s.replace(\"\"\"  bool adaptiveCf |
| medium | suspicious_patterns | `3a8e926a` | d": "ps aux | grep \"[_]km1\" | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 2\npython3 - <<'PY'\np='src/core/CaseConfig.h'; s=open(p).read()\ns=s.replace(\"\"\"  long limiterFre |

## Reviews

- Code review scorecard: `claude_generic_opus-01_9a712c/review_code.md` (overall: 4.45)
- CFD methods review: `claude_generic_opus-01_9a712c/review_cfd.md` (overall: 4.92)
- Result review: `claude_generic_opus-01_9a712c/review_results.md` (overall: 4.7)

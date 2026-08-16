# Final Result Summary — 02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/02`
- Branch: `codex/glm52-m3/02` commit `6148b7b8525000945d5591faceda30aeb1bc1180`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/02/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/02/solver/report`)
- Session window: 2026-08-01T21:49:51.169000+00:00 → 2026-08-02T09:10:45.277000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-01T21:49:51.169000+00:00 → 2026-08-02T09:10:45.277000+00:00; 23×1800s buckets; idle 2 gaps / 31745s excluded; permission-wait candidates 0; tokens 102,546,862 (cache hit 0.9723)

## Expenses

- Goal time (codex): **9086 s**
- Wall time: **40854 s**
- Tokens: **102,546,862** (main 102,426,064 / subagents 120,798)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fbf4d` | complete | BLSC/GLM-5.2 | 2 | 102,546,862 | 9086 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/GLM-5.2 | 102,071,820 | 99,266,624 | 354,244 | 102,426,064 |
| BLSC/MiniMax-M3 | 117,812 | 90,112 | 2,986 | 120,798 |

- Cost estimate: **$33.44** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **497**; top tools: exec_command=354, apply_patch=132, view_image=5, update_plan=3, spawn_agent=1, wait_agent=1
- Subagent spawns: 1
- LOC (file scan): 3,742 lines / 15 files
- LOC (git tracked): 3,742 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 849d35819a6b (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/GLM-5.2 | max | 522500 | 320,494 | 1 |
| BLSC/MiniMax-M3 | medium | 522500 | 28,314 | 1 |

### opencodex router (non-vanilla models: BLSC/GLM-5.2, BLSC/MiniMax-M3)

- opencodex version: None (submodule None)
- config facts: {}

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fc19f` | `019fbf4d` | Zeno | visual_review | BLSC/MiniMax-M3 | medium | 120,798 |

### Prompts

- `019fbf4d` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `019fbf4d` | 5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_02 && git checkout -b solver/glm52-m3/build 2>&1 && mkdir -p solver/src solver/tools solver/report/figures solver/results && echo "Branch a |
| medium | network_access | `019fbf4d` | -m venv solver/.venv && source solver/.venv/bin/activate && pip install numpy matplotlib pyvista 2>&1 | tail -5 |
| medium | network_access | `019fbf4d` | 52_m3_02 && source solver/.venv/bin/activate 2>/dev/null && pip install numpy matplotlib 2>&1 | tail -3 |
| medium | network_access | `019fbf4d` | pkill -f "pip install" 2>/dev/null; cd /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_02 && python3 --version && pip3 |
| medium | suspicious_patterns | `019fbf4d` | pkill -9 -f cfd_solver 2>/dev/null; pkill -9 -f prterun 2>/dev/null; sleep 2; echo "cleaned" |

## Reviews

- Code review scorecard: `codex_glm52-m3_02_ca46e4/review_code.md` (overall: 2.92)
- CFD methods review: `codex_glm52-m3_02_ca46e4/review_cfd.md` (overall: 3.42)
- Result review: `codex_glm52-m3_02_ca46e4/review_results.md` (overall: 0.3)

# Final Result Summary — 05

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/05`
- Branch: `codex/glm52-m3/05` commit `0065953716e70f087f5d6d4fb512bc7e2fe2ba97`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/05/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/05/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52-m3/05/solver/report`)
- Session window: 2026-08-09T21:15:51.533000+00:00 → 2026-08-10T03:44:08.214000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-09T21:15:51.533000+00:00 → 2026-08-10T03:44:08.214000+00:00; 13×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 241,544,063 (cache hit 0.9866)

## Expenses

- Goal time: **23287 s**
- Wall time: **23297 s**
- Tokens: **241,544,063** (main 241,544,063 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fe861` | complete | BLSC/GLM-5.2 | 1 | 241,544,063 | 23287 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/GLM-5.2 | 241,014,119 | 237,774,784 | 529,944 | 241,544,063 |

- Cost estimate: **$25.92** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **749**; top tools: exec_command=501, write_stdin=225, apply_patch=11, update_plan=10, interrupt_agent=1, update_goal=1
- Subagent spawns: 0
- LOC (file scan): 94,843 lines / 282 files
- LOC (git tracked): 3,194 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 838fb21075bb (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/GLM-5.2 | ultra | 550000 | 32,764,064,310 | 1 |

### opencodex router (non-vanilla models: BLSC/GLM-5.2)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "openai", "opencode-free"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "BLSC/GLM-5.2", "BLSC/MiniMax-M3", "deepseek/deepseek-v4-flash"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSeek-V3-250324", "BLSC/DeepSeek-R1-0528", "BL

### Prompts

- `019fe861` goal: Complete the goal defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the goal defined in cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fe861` | for plotting python3 -m venv .venv 2>&1 | tail -2 .venv/bin/pip install --quiet numpy matplotlib 2>&1 | tail -3 .venv/bin/python -c "import numpy, matplotlib; print('numpy', numpy.__version__ |
| medium | network_access | `019fe861` | solver && python3 -m venv .venv 2>&1 | tail -2 && .venv/bin/pip install --quiet numpy matplotlib 2>&1 | tail -2 && .venv/bin/python -c "import numpy,matplotlib; print('ok', numpy.__version__) |
| medium | network_access | `019fe861` | plots / artifacts)  ```bash python3 -m venv .venv .venv/bin/pip install numpy matplotlib .venv/bin/python tools/plot_results.py     # figures -> report/figures/ .venv/bin/python tools/make_ar |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace && git checkout -b solver/attempt1-glm52 2>&1 | tail -2 && git add solver/ 2>&1 | tail -3 && git status --short 2>&1 | head -15 |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace/solver && git checkout src/solver.cpp && echo "Restored solver.cpp from git" && grep -c "CHECK\|DEBUG" src/solver.cpp |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace/solver && git checkout src/solver.cpp && python3 - <<'PYEOF' p = 'src/solver.cpp' s = open(p).read()  # 1. Remove the LUSGS_DBG block import re |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace/solver && git checkout src/solver.cpp && grep -n "LUSGS_DBG\|step<=3.*printf\|startup_steps = 100000" src/solver.cpp |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace/solver && git checkout src/solver.cpp && python3 - <<'PYEOF' p = 'src/solver.cpp' s = open(p).read() # Re-apply: damping=3.0, Ubest after visc_ |
| high | unauthorized_remote_mutations | `019fe861` | cd /workspace/solver && git checkout src/solver.cpp && python3 - <<'PYEOF' p = 'src/solver.cpp' s = open(p).read() # Re-apply the working settings: damping=3 |

## Reviews

- Code review scorecard: `codex_glm52-m3_05_f094c1/review_code.md` (overall: 3.2)
- CFD methods review: `codex_glm52-m3_05_f094c1/review_cfd.md` (overall: 3.22)
- Result review: `codex_glm52-m3_05_f094c1/review_results.md` (overall: 1.1)

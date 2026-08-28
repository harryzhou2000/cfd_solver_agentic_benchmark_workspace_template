# Final Result Summary — 06

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/06`
- Branch: `codex/kimik3/06` commit `60a259515e88a436ca47c6c9b1d6157db38eb4c2`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/06/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/06/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/06/solver/report`)
- Session window: 2026-08-25T07:01:50.018000+00:00 → 2026-08-27T13:33:17.771000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-25T07:01:50.018000+00:00 → 2026-08-27T13:33:17.771000+00:00; 110×1800s buckets; idle 7 gaps / 169280s excluded; permission-wait candidates 0; tokens 107,091,129 (cache hit 0.9816)

## Expenses

- Goal time: **26825 s**
- Wall time: **196288 s**
- Tokens: **107,091,129** (main 88,384,735 / subagents 18,706,394)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a037b9` | complete | kimi-code/k3 | 9 | 107,091,129 | 26825 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| kimi-code/k3 | 106,288,312 | 104,337,293 | 802,817 | 107,091,129 |

- Cost estimate: **unavailable** (unpriced tokens: 107,091,129)

## Measurements

- Tool calls: **984**; top tools: exec=944, wait_agent=14, wait=12, spawn_agent=9, list_agents=5
- Subagent spawns: 8
- LOC (file scan): 5,122 lines / 21 files
- LOC (git tracked): 5,122 lines

## Metadata

- Harness: codex cli 0.149.1 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 8fee0d6b87d5 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| kimi-code/k3 | ultra | 262144 | 77,128,136 | 9 |

### opencodex router (non-vanilla models: kimi-code/k3)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "internal_eccn", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a03825` | `01a037b9` | Bacon | plot_pipeline | kimi-code/k3 | ultra | 1,955,770 |
| `01a0383b` | `01a03825` | Maxwell | code_audit | kimi-code/k3 | ultra | 886,139 |
| `01a03894` | `01a037b9` | Carson | report_draft | kimi-code/k3 | ultra | 9,755,648 |
| `01a03895` | `01a03894` | Kepler | source_summary | kimi-code/k3 | ultra | 905,397 |
| `01a038b1` | `01a03894` | Mendel | report_review | kimi-code/k3 | ultra | 1,216,478 |
| `01a03995` | `01a037b9` | Huygens | final_code_audit | kimi-code/k3 | ultra | 2,798,234 |
| `01a03996` | `01a03995` | Kuhn | audit_spatial | kimi-code/k3 | ultra | 436,305 |
| `01a03996` | `01a03995` | Wegener | audit_timestep | kimi-code/k3 | ultra | 752,423 |

### Prompts

- `01a037b9` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: 0;10;1c
  - resume: /goal resume
  - resume: /goal resume

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `01a037b9` | const r = await tools.exec_command({cmd: `cd /workspace && git checkout -b solver/attempt-k3 2>&1 && git config user.name "k3-cfd-agent" && git config user.email "k3-cfd-agent@local" && mkdir  |
| medium | network_access | `01a037b9` | d /workspace/solver && python3 -m venv .venv && ./.venv/bin/pip install --quiet --upgrade pip 2>&1 | tail -1; ./.venv/bin/pip install --quiet numpy matplotlib 2>&1 | tail -2; ./.venv/bin/pyth |
| medium | suspicious_patterns | `01a037b9` |  = await tools.exec_command({cmd: "cd /workspace/solver && pkill -9 -f steadyinit 2>/dev/null; sleep 2; ps aux | grep '[s]teadyinit' | wc -l; head -8 cases/cylinder_m010_laminar_re200_stea |

## Reviews

- Code review scorecard: `codex_kimik3_06_8208e5/review_code.md` (overall: 4.47)
- CFD methods review: `codex_kimik3_06_8208e5/review_cfd.md` (overall: 4.46)
- Result review: `codex_kimik3_06_8208e5/review_results.md` (overall: 4.8)

# Final Result Summary — opus46-01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-01`
- Branch: `codex/generic/opus46-01` commit `f6796449524c8b7e5f17c021cdc92a5a4194b17a`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-01/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-01/solver/report`)
- Session window: 2026-08-24T14:03:41.157000+00:00 → 2026-08-24T16:13:38.448000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-24T14:03:41.157000+00:00 → 2026-08-24T16:13:38.448000+00:00; 5×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 78,666,333 (cache hit 0.9938)

## Expenses

- Goal time: **7790 s**
- Wall time: **7797 s**
- Tokens: **78,666,333** (main 73,040,715 / subagents 5,625,618)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a0340e` | complete | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | 14 | 78,666,333 | 7790 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | 78,385,257 | 77,898,800 | 281,076 | 78,666,333 |

- Cost estimate: **unavailable** (unpriced tokens: 78,666,333)

## Measurements

- Tool calls: **579**; top tools: exec=503, wait=34, spawn_agent=20, wait_agent=10, apply_patch=9, list_agents=3
- Subagent spawns: 13
- LOC (file scan): 4,255 lines / 29 files
- LOC (git tracked): 4,255 lines

## Metadata

- Harness: codex cli 0.148.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 7aa6dde4c59a (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 550000 | 1,521,570 | 14 |

### opencodex router (non-vanilla models: internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "internal_eccn", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a03419` | `01a0340e` | Dewey | write_mesh_reader | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 1,071,557 |
| `01a0341a` | `01a0340e` | Kant | write_partitioner | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 447,405 |
| `01a0341a` | `01a0340e` | Ramanujan | write_fluxes | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 247,456 |
| `01a03443` | `01a0340e` | Descartes | run_cases | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 520,386 |
| `01a03444` | `01a0340e` | Euler | write_plotting | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 474,543 |
| `01a03444` | `01a03443` | Euclid | run_case2 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 217,063 |
| `01a03446` | `01a03443` | Curie | run_case3 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 166,530 |
| `01a03447` | `01a03443` | Sagan | run_case4 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 108,123 |
| `01a03449` | `01a03443` | Cicero | run_case5 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 302,866 |
| `01a03451` | `01a0340e` | Wegener | write_report | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 344,546 |
| `01a03451` | `01a03443` | Banach | run_case6 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 86,119 |
| `01a03452` | `01a03443` | Anscombe | run_case7 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 107,982 |
| `01a03454` | `01a03443` | Heisenberg | run_case8 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 1,531,042 |

### Prompts

- `01a0340e` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `01a0340e` | ce/solver && python3 -m venv .venv 2>/dev/null && .venv/bin/pip install numpy matplotlib 2>&1 | tail -3`,   workdir: "/workspace",   yield_time_ms: 30000 }); text(r.output);  |
| medium | network_access | `01a0340e` | ent: `python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib`\n   - Plotting: `.venv/bin/python tools/plot_results.py`\n\n2. **/workspace/solver/report/report.tex* |
| medium | network_access | `01a03444` | er && python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib 2>&1 | tail -5",   yield_time_ms: 30000 }); text(result.output);  |
| medium | network_access | `01a03451` | n```\npython3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib\n```\n\nPlotting:\n```\n.venv/bin/python tools/plot_results.py\n```\n\nWrite this as a well-structured |
| medium | network_access | `01a03451` | bash +python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib +\`\`\` + +## Plotting + +Generate result figures (residual convergence, force histories, Cp +distribu |
| medium | network_access | `01a03451` | bash +python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib +\`\`\` + +## Plotting + +Generate result figures (residual convergence, force histories, Cp +distribu |
| medium | network_access | `01a03451` | `bash python3 -m venv .venv && source .venv/bin/activate && pip install numpy matplotlib \`\`\`  ## Plotting  Generate result figures (residual convergence, force histories, Cp distributions, |
| high | unauthorized_remote_mutations | `01a0340e` | nst r = await tools.exec_command({   cmd: `cd /workspace && git checkout -b solver/cfd2d-submission 2>&1`,   workdir: "/workspace" }); text(r.output);  |
| medium | suspicious_patterns | `01a03454` | start fresh const killAll = await tools.exec_command({cmd: "kill -9 7792 7796 2>/dev/null; sleep 2; ps aux | grep cfd2d | grep -v grep", workdir: "/workspace/solver", login: false}); text( |

## Reviews

- Code review scorecard: `codex_generic_opus46-01_3a72ea/review_code.md` (overall: 3.85)
- CFD methods review: `codex_generic_opus46-01_3a72ea/review_cfd.md` (overall: 3.73)
- Result review: `codex_generic_opus46-01_3a72ea/review_results.md` (overall: 3.8)

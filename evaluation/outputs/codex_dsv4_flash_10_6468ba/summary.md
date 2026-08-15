# Final Result Summary — 10

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/10`
- Branch: `codex/dsv4_flash/10` commit `47dff36bef32a0a978e276e3a2683e14444c2acc`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/10/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/10/solver/run_clean_checkout`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/10/solver/report`)
- Session window: 2026-08-08T16:46:09.587000+00:00 → 2026-08-09T10:03:05.428000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-08T16:46:09.587000+00:00 → 2026-08-09T10:03:05.428000+00:00; 35×1800s buckets; idle 1 gaps / 42082s excluded; permission-wait candidates 0; tokens 209,379,871 (cache hit 0.984)

## Expenses

- Goal time (codex): **20119 s**
- Wall time: **62216 s**
- Tokens: **209,379,871** (main 187,933,117 / subagents 21,446,754)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fe243` | complete | deepseek/deepseek-v4-flash | 9 | 209,379,871 | 20119 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/DeepSeek-V4-Flash | 407,835 | 316,416 | 2,190 | 410,025 |
| deepseek/deepseek-v4-flash | 207,834,632 | 204,594,560 | 1,135,214 | 208,969,846 |
| gpt-5.6-luna | 0 | 0 | 0 | 0 |

- Cost estimate: **$7.15** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **1,084**; top tools: exec_command=710, apply_patch=184, write_stdin=146, spawn_agent=9, wait_agent=9, update_plan=8
- Subagent spawns: 8
- LOC (file scan): 17,221 lines / 86 files
- LOC (git tracked): 5,831 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 10fe7826ce35 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/DeepSeek-V4-Flash | medium | 550000 | 67,412 | 2 |
| deepseek/deepseek-v4-flash | ultra, xhigh, high | 550000 | 10,830,652,175 | 5 |
| gpt-5.6-luna | medium | 372000 | n/a | 2 |

### opencodex router (non-vanilla models: BLSC/DeepSeek-V4-Flash, deepseek/deepseek-v4-flash)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "openai", "opencode-free"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "BLSC/GLM-5.2", "BLSC/MiniMax-M3", "deepseek/deepseek-v4-flash"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSeek-V3-250324", "BLSC/DeepSeek-R1-0528", "BL

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fe27e` | `019fe243` | Chandrasekhar | solver_convergence_audit | deepseek/deepseek-v4-flash | xhigh | 6,039,663 |
| `019fe281` | `019fe27e` | Kuhn | audit_flux_mesh | deepseek/deepseek-v4-flash | high | 12,154,726 |
| `019fe281` | `019fe27e` | Linnaeus | audit_solver_implicit | deepseek/deepseek-v4-flash | high | 685,577 |
| `019fe30b` | `019fe243` | Avicenna | solver_final_review | deepseek/deepseek-v4-flash | high | 2,156,763 |
| `019fe30c` | `019fe30b` | Nash | audit_numerics | BLSC/DeepSeek-V4-Flash | medium | 156,810 |
| `019fe30c` | `019fe30b` | Parfit | audit_outputs | BLSC/DeepSeek-V4-Flash | medium | 253,215 |
| `019fe30d` | `019fe30b` | Aristotle | audit_numerics2 | gpt-5.6-luna | medium | 0 |
| `019fe30d` | `019fe30b` | Beauvoir | audit_outputs2 | gpt-5.6-luna | medium | 0 |

### Prompts

- `019fe243` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal Complete the task defined in cfd_solver_agentic_benchmark/ . 

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fe243` | /tmp/venvtest 2>&1 | tail -1; timeout 120 /tmp/venvtest/bin/pip install --quiet numpy 2>&1 | tail -3; /tmp/venvtest/bin/python -c "import numpy; print(numpy.__version__)" 2>&1 | tail -1 |
| medium | network_access | `019fe243` | && cd /workspace/solver; python3 -m venv .venv && .venv/bin/pip install --quiet --upgrade pip 2>&1 | tail -1; .venv/bin/pip install --quiet numpy matplotlib 2>&1 | tail -3; .venv/bin/python - |
| medium | network_access | `019fe243` | df -h /workspace | tail -1; command -v curl wget; curl -sI --max-time 15 https://github.com | head -3; echo "curl_exit=$?" |
| medium | network_access | `019fe243` | mkdir -p /workspace/bin && curl -sL --max-time 30 https://api.github.com/repos/tectonic-typesetting/tectonic/releases/latest | python3 -c 'import json,s |
| medium | network_access | `019fe243` | cd /workspace/bin && curl -sL --max-time 120 -o tectonic.tar.gz https://github.com/tectonic-typesetting/tectonic/releases/download/tectonic%400.17 |
| medium | network_access | `019fe243` | cd /workspace/bin && curl -sL --max-time 120 -o tectonic-musl.tar.gz https://github.com/tectonic-typesetting/tectonic/releases/download/tectonic%4 |
| medium | network_access | `019fe243` |  | head -5; ls -d /workspace/external/nlohmann 2>/dev/null; git clone --quiet /workspace /workspace/solver/run_clean_checkout && ln -s ../../external /workspace/solver/run_clean_checkout/ext |
| high | unauthorized_remote_mutations | `019fe243` | cd /workspace && git checkout -b solver/attempt1 2>&1; mkdir -p solver/src solver/tools solver/scripts solver/report/figures solver/results solver/tes |

## Reviews

- Code review scorecard: `codex_dsv4_flash_10_6468ba/review_code.md` (overall: 3.75)
- CFD methods review: `codex_dsv4_flash_10_6468ba/review_cfd.md` (overall: 4.0)
- Result review: `codex_dsv4_flash_10_6468ba/review_results.md` (overall: 4.8)

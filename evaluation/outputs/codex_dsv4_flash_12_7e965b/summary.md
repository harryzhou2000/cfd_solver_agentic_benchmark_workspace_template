# Final Result Summary — 12

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/12`
- Branch: `codex/dsv4_flash/12` commit `97bdc69583d3f31441466cb7349d5632494e3897`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/12/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/12/old_solver/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/12/solver/report`)
- Session window: 2026-08-13T08:34:57.704000+00:00 → 2026-08-14T08:34:34.155000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-13T08:34:57.704000+00:00 → 2026-08-14T08:34:34.155000+00:00; 48×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 540,699,535 (cache hit 0.996)

## Expenses

- Goal time: **86367 s**
- Wall time: **86376 s**
- Tokens: **540,699,535** (main 514,853,838 / subagents 25,845,697)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019ffa41` | complete | deepseek/deepseek-v4-pro | 8 | 540,699,535 | 86367 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/DeepSeek-V4-Flash | 25,524,665 | 24,454,656 | 321,032 | 25,845,697 |
| deepseek/deepseek-v4-pro | 513,986,050 | 512,921,344 | 867,788 | 514,853,838 |

- Cost estimate: **$31.90** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **2,154**; top tools: exec_command=1385, write_stdin=442, apply_patch=281, list_agents=10, wait_agent=10, spawn_agent=9
- Subagent spawns: 7
- LOC (file scan): 19,434 lines / 84 files
- LOC (git tracked): 7,561 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 10fe7826ce35 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/DeepSeek-V4-Flash | medium | 550000 | 70,524,916 | 7 |
| deepseek/deepseek-v4-pro | ultra | 550000 | 43,288,144,200 | 1 |

### opencodex router (non-vanilla models: BLSC/DeepSeek-V4-Flash, deepseek/deepseek-v4-pro)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "openai", "opencode-free"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "BLSC/GLM-5.2", "BLSC/MiniMax-M3", "deepseek/deepseek-v4-flash"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSeek-V3-250324", "BLSC/DeepSeek-R1-0528", "BL

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019ffb04` | `019ffa41` | Erdos | numerics_audit | BLSC/DeepSeek-V4-Flash | medium | 6,481,212 |
| `019ffb05` | `019ffb04` | Rawls | audit_physics | BLSC/DeepSeek-V4-Flash | medium | 6,496,385 |
| `019ffb05` | `019ffb04` | Ohm | audit_solver | BLSC/DeepSeek-V4-Flash | medium | 3,037,592 |
| `019fff50` | `019ffa41` | Hubble | compliance_audit | BLSC/DeepSeek-V4-Flash | medium | 4,401,306 |
| `019fff51` | `019fff50` | Mendel | results_audit | BLSC/DeepSeek-V4-Flash | medium | 1,128,614 |
| `019fff51` | `019fff50` | Banach | report_audit | BLSC/DeepSeek-V4-Flash | medium | 2,727,714 |
| `019fff56` | `019fff50` | Lovelace | source_audit | BLSC/DeepSeek-V4-Flash | medium | 1,572,874 |

### Prompts

- `019ffa41` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `019ffa41` | git checkout -b solver/main 2>&1 | head -3; git branch --show-current |
| medium | network_access | `019ffa41` | cd /workspace/solver && python3 -m venv .venv && .venv/bin/pip install --quiet --upgrade pip && .venv/bin/pip install --quiet numpy matplotlib |
| medium | network_access | `019ffa41` | on environment + +```bash +python3 -m venv .venv +.venv/bin/pip install numpy matplotlib +``` + +## Post-processing + +```bash +.venv/bin/python tools/plot_histories.py results/<case> +.venv/ |
| medium | network_access | `019ffb05` | /workspace/solver/.venv/bin/pip install -q h5py 2>&1 | tail -2; /workspace/solver/.venv/bin/python -c "import h5py; print('h5py', h5py.__version__)" |
| medium | network_access | `019ffb05` | /workspace/solver/.venv/bin/pip install -q scipy 2>&1 | tail -1; /workspace/solver/.venv/bin/python - <<'EOF' import h5py, numpy as np from scipy.spatial impor |

## Reviews

- Code review scorecard: `codex_dsv4_flash_12_7e965b/review_code.md` (overall: 4.07)
- CFD methods review: `codex_dsv4_flash_12_7e965b/review_cfd.md` (overall: 4.83)
- Result review: `codex_dsv4_flash_12_7e965b/review_results.md` (overall: 4.25)

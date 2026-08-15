# Final Result Summary — 06

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/06`
- Branch: `codex/gpt56/06` commit `55b24264cfb94fe01c32133e30885c75cb2e17bf`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/06/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/06`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/06/solver/report`)
- Session window: 2026-08-06T19:39:16.488000+00:00 → 2026-08-09T07:15:42.811000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-06T19:39:16.488000+00:00 → 2026-08-09T07:15:42.811000+00:00; 120×1800s buckets; idle 1 gaps / 188143s excluded; permission-wait candidates 0; tokens 404,346,109 (cache hit 0.9766)

## Expenses

- Goal time (codex): **1476 s**
- Wall time: **214586 s**
- Tokens: **404,346,109** (main 270,739,414 / subagents 133,606,695)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fd895` | complete | gpt-5.6-luna | 6 | 404,346,109 | 1476 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-luna | 270,283,951 | 265,562,624 | 455,463 | 270,739,414 |
| gpt-5.6-sol | 15,917,241 | 14,755,840 | 55,992 | 15,973,233 |
| gpt-5.6-terra | 117,287,010 | 113,723,904 | 346,452 | 117,633,462 |

- Cost estimate: **$84.73** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **2,104**; top tools: exec=1663, wait=355, send_message=40, list_agents=15, followup_task=14, wait_agent=10
- Subagent spawns: 5
- LOC (file scan): 8,619 lines / 31 files
- LOC (git tracked): 2,736 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai_vanilla)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 0a05c72c06d3 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-luna | max | 372000 | 40,391,002,327 | 1 |
| gpt-5.6-sol | max, medium | 372000 | 2,628,527 | 1 |
| gpt-5.6-terra | max, medium | 372000 | 3,738,262 | 4 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fd896` | `019fd895` | Pasteur | solver_core | gpt-5.6-terra | max, medium | 2,907,759 |
| `019fd897` | `019fd895` | Raman | analysis_reporting | gpt-5.6-sol | max, medium | 15,973,233 |
| `019fd897` | `019fd895` | Heisenberg | mesh_case_inspection | gpt-5.6-terra | max, medium | 1,589,755 |
| `019fd8e5` | `019fd895` | James | mesh_audit | gpt-5.6-terra | max, medium | 55,322,003 |
| `019fd8e5` | `019fd895` | Ohm | numerics | gpt-5.6-terra | max, medium | 57,813,945 |

### Prompts

- `019fd895` goal: complete the task defined in cfd_solver_agentic_benchmark/ . Latex provided.
  - initial: /goal complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal complete the task defined in cfd_solver_agentic_benchmark/ . Latex provided.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fd895` | mand({cmd:"python3 -m venv solver/.venv && solver/.venv/bin/pip install -q -r solver/tools/requirements.txt && solver/.venv/bin/python - <<'PY'\nimport numpy,matplotlib; print('deps',numpy.__ |
| medium | network_access | `019fd897` | or use the benchmark environment:\n+\n+```bash\n+python3 -m pip install -r solver/tools/requirements.txt\n+```\n+\n+`pdflatex` is optional. ASCII VTU and legacy ASCII VTK field files are\n+su |
| medium | network_access | `019fd897` | hon3 -m venv solver/.report-venv && solver/.report-venv/bin/pip install -r solver/tools/requirements.txt","workdir":"/workspace","yield_time_ms":1000,"max_output_tokens":12000}); text(JSON.st |

## Reviews

- Code review scorecard: `codex_gpt56_06_97ae53/review_code.md` (overall: 3.73)
- CFD methods review: `codex_gpt56_06_97ae53/review_cfd.md` (overall: 3.91)
- Result review: `codex_gpt56_06_97ae53/review_results.md` (overall: 4.4)

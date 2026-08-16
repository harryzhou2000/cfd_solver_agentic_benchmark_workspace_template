# Final Result Summary — 01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/01`
- Branch: `codex/gpt56/01` commit `af2fcc3ebfb2f58b8256f3b7604ba1b4373dfb4c`
- Benchmark submodule: `ba3f9a8b1443d43c9d4ec1fdd110b8d112cd6701`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/01/cfd_solver_agentic_benchmark/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/01/cfd_solver_agentic_benchmark/report`)
- Session window: 2026-07-31T20:12:24.744000+00:00 → 2026-08-01T02:17:10.168000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-07-31T20:37:34.058000+00:00 → 2026-08-01T02:17:10.168000+00:00; 12×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 212,765,117 (cache hit 0.9687)

## Expenses

- Goal time (codex): **20354 s**
- Wall time: **20376 s**
- Tokens: **212,765,117** (main 140,579,327 / subagents 72,185,790)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fb9e3` | complete | gpt-5.6-sol | 22 | 212,765,117 | 20354 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-sol | 166,268,883 | 161,963,520 | 370,151 | 166,639,034 |
| gpt-5.6-terra | 45,759,430 | 43,428,608 | 366,653 | 46,126,083 |

- Cost estimate: **$131.99** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **1,937**; top tools: exec=1499, wait=250, send_message=78, wait_agent=48, followup_task=26, spawn_agent=22
- Subagent spawns: 21
- LOC (file scan): 19,318 lines / 69 files
- LOC (git tracked): 19,318 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 8c9f03983ec1 (matches HEAD)
- CodeGraph: absent
- Benchmark submodule: ba3f9a8b1443 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-sol | ultra, high | 258400 | 225,037 | 3 |
| gpt-5.6-terra | ultra, high, max | 258400 | 222,162 | 19 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fb9e5` | `019fb9e3` | Bernoulli | spec_requirements | gpt-5.6-terra | ultra, high | 3,230,410 |
| `019fb9e5` | `019fb9e3` | Herschel | validation_requirements | gpt-5.6-terra | ultra, high | 3,724,209 |
| `019fb9e5` | `019fb9e3` | Hume | state_audit | gpt-5.6-terra | ultra, high | 2,715,005 |
| `019fb9f4` | `019fb9e3` | Parfit | report_tooling | gpt-5.6-terra | ultra, high | 3,917,382 |
| `019fba0e` | `019fb9e3` | Ptolemy | newton_diagnosis | gpt-5.6-terra | ultra, high | 15,365,421 |
| `019fba0e` | `019fb9e3` | Franklin | spatial_audit | gpt-5.6-terra | ultra, high | 15,683,030 |
| `019fba0e` | `019fb9e3` | Helmholtz | contract_audit | gpt-5.6-terra | ultra, high | 15,771,564 |
| `019fba34` | `019fb9e3` | Planck | readme_rewrite | gpt-5.6-terra | ultra, high | 35,580,142 |
| `019fba34` | `019fb9e3` | James | postprocess_qc | gpt-5.6-terra | ultra, high | 36,430,562 |
| `019fba34` | `019fb9e3` | Euclid | report_audit_fix | gpt-5.6-terra | ultra, high | 36,993,891 |
| `019fba4a` | `019fb9e3` | Boole | live_results_audit | gpt-5.6-terra | high | 1,590,094 |
| `019fba4a` | `019fb9e3` | Laplace | final_requirements_audit | gpt-5.6-terra | ultra, high | 52,575,517 |
| `019fba4c` | `019fb9e3` | Faraday | report_compliance_patch | gpt-5.6-sol | ultra, high | 79,299,696 |
| `019fba5b` | `019fb9e3` | Schrodinger | m2_convergence_diagnosis | gpt-5.6-terra | ultra, max | 64,220,554 |
| `019fba64` | `019fb9e3` | Anscombe | recovery_patch_review | gpt-5.6-terra | high | 432,234 |
| `019fba69` | `019fb9e3` | Goodall | force_figure_quality | gpt-5.6-terra | ultra, high | 61,174,347 |
| `019fba6f` | `019fb9e3` | Volta | cylinder_re20_diagnosis | gpt-5.6-terra | ultra, max | 66,520,550 |
| `019fba8b` | `019fb9e3` | Bacon | corrected_laminar_audit | gpt-5.6-terra | ultra, high | 80,212,300 |
| `019fba8b` | `019fb9e3` | Avicenna | re200_gate_audit | gpt-5.6-sol | ultra, high | 81,441,707 |
| `019fba8f` | `019fb9e3` | Ramanujan | fft_robustness | gpt-5.6-terra | ultra, high | 86,978,829 |
| `019fbad7` | `019fba4c` | Banach | report_patch_review | gpt-5.6-terra | ultra, high | 62,143,189 |

### Prompts

- `019fb9e3` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fb9e3` | _command({cmd:"python3 -m venv .venv && .venv/bin/python -m pip install --disable-pip-version-check -r requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cod |
| medium | network_access | `019fba34` | \n+\n+```bash\n+python3 -m venv .venv\n+.venv/bin/python -m pip install --upgrade pip\n+.venv/bin/python -m pip install -r requirements.txt\n+```\n+\n+## Configure, build, and test\n+\n+The f |

## Reviews

- Code review scorecard: `codex_gpt56_01_cb349f/review_code.md` (overall: 4.2)
- CFD methods review: `codex_gpt56_01_cb349f/review_cfd.md` (overall: 4.92)
- Result review: `codex_gpt56_01_cb349f/review_results.md` (overall: 4.85)

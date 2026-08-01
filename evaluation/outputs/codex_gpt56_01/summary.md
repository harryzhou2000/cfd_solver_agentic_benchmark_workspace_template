# Final Result Summary — codex_gpt56_01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_01`
- Branch: `codex/gpt56/work01` commit `727cbfd4891b661a99ad51f0ef42686c90f00c08`
- Benchmark submodule: `ba3f9a8b1443d43c9d4ec1fdd110b8d112cd6701`
- Layout: non_standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_01/cfd_solver_agentic_benchmark/src`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_01/cfd_solver_agentic_benchmark/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_01/cfd_solver_agentic_benchmark/report`)

## Expenses

- Goal time (codex): **20978 s**
- Wall time: **21885 s**
- Tokens: **954,876,689** (main 141,947,209 / subagents 813,040,729)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fb9e3` | complete | gpt-5.6-sol | 22 | 946,579,960 | 20354 |
| `019fb9cd` | paused | gpt-5.6-sol | 13 | 6,799,649 | 381 |
| `019fb9df` | blocked | gpt-5.6-sol | 4 | 1,010,966 | 135 |
| `019fb9d5` | paused | gpt-5.6-sol | 7 | 597,363 | 108 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-luna | 1,572,880 | 1,235,968 | 18,262 | 3,413,672 |
| gpt-5.6-sol | 99,942,773 | 88,059,850 | 299,174 | 242,189,156 |
| gpt-5.6-terra | 704,064,150 | 662,894,959 | 5,209,711 | 709,273,861 |

- Cost estimate: **$388.89** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **2,166**; top tools: exec=1681, wait=250, send_message=91, wait_agent=54, spawn_agent=44, followup_task=26
- Subagent spawns: 42
- LOC (file scan): 9,821 lines / 35 files
- LOC (git tracked): 9,821 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-luna | high, xhigh | 372000 | 907,477 | 9 |
| gpt-5.6-sol | ultra, high | 372000 | 8,954,891 | 6 |
| gpt-5.6-terra | high, xhigh, max, ultra | 372000 | 4,743,197 | 31 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fb9ce` | `019fb9cd` | Lagrange | requirements_audit | gpt-5.6-terra | high | 138,614 |
| `019fb9cf` | `019fb9cd` | Linnaeus | validator_audit | gpt-5.6-terra | xhigh | 206,292 |
| `019fb9cf` | `019fb9cd` | Halley | dependency_audit | gpt-5.6-luna | high | 190,259 |
| `019fb9cf` | `019fb9cd` | Kant | solver_design | gpt-5.6-terra | max | 84,593 |
| `019fb9cf` | `019fb9cd` | Turing | mesh_analysis | gpt-5.6-luna | xhigh | 126,937 |
| `019fb9d2` | `019fb9cd` | Huygens | mesh_forensics | gpt-5.6-terra | high | 496,428 |
| `019fb9d2` | `019fb9cd` | Ohm | validator_deep | gpt-5.6-terra | high | 218,102 |
| `019fb9d2` | `019fb9cd` | Sagan | numerical_architecture | gpt-5.6-terra | max | 997,830 |
| `019fb9d2` | `019fb9cd` | Avicenna | dependency_probe | gpt-5.6-luna | high | 448,422 |
| `019fb9d3` | `019fb9cd` | Rawls | output_report_design | gpt-5.6-luna | high | 916,615 |
| `019fb9d3` | `019fb9cd` | Franklin | runtime_strategy | gpt-5.6-terra | max | 758,686 |
| `019fb9d3` | `019fb9cd` | Gibbs | test_design | gpt-5.6-luna | high | 1,374,108 |
| `019fb9d6` | `019fb9d5` | Kepler | requirements_audit | gpt-5.6-terra | high | 139,055 |
| `019fb9d6` | `019fb9d5` | Euclid | validator_audit | gpt-5.6-terra | high | 64,658 |
| `019fb9d6` | `019fb9d5` | Hubble | numerics_audit | gpt-5.6-terra | max | 85,326 |
| `019fb9d6` | `019fb9d5` | Zeno | report_audit | gpt-5.6-luna | high | 89,377 |
| `019fb9d6` | `019fb9d5` | Raman | runtime_assets | gpt-5.6-luna | high | 36,489 |
| `019fb9d6` | `019fb9d5` | Lorentz | mesh_inspection | gpt-5.6-luna | high | 0 |
| `019fb9e0` | `019fb9df` | Halley | requirements_audit | gpt-5.6-terra | high | 309,991 |
| `019fb9e0` | `019fb9df` | Copernicus | validator_audit | gpt-5.6-luna | high | 231,465 |
| `019fb9e0` | `019fb9df` | Faraday | environment_strategy | gpt-5.6-terra | high | 126,849 |
| `019fb9e5` | `019fb9e3` | Bernoulli | spec_requirements | gpt-5.6-terra | ultra, high | 3,230,410 |
| `019fb9e5` | `019fb9e3` | Herschel | validation_requirements | gpt-5.6-terra | ultra, high | 3,724,209 |
| `019fb9e5` | `019fb9e3` | Hume | state_audit | gpt-5.6-terra | ultra, high | 2,715,005 |
| `019fb9f4` | `019fb9e3` | Parfit | report_tooling | gpt-5.6-terra | ultra, high | 3,917,382 |
| `019fba0e` | `019fb9e3` | Ptolemy | newton_diagnosis | gpt-5.6-terra | ultra, high | 15,365,421 |
| `019fba0e` | `019fb9e3` | Franklin | spatial_audit | gpt-5.6-terra | ultra, high | 15,683,030 |
| `019fba0e` | `019fb9e3` | Helmholtz | contract_audit | gpt-5.6-terra | ultra, high | 15,771,564 |
| `019fba34` | `019fb9e3` | Planck | readme_rewrite | gpt-5.6-terra | ultra, high | 35,580,142 |
| `019fba34` | `019fb9e3` | James | postprocess_qc | gpt-5.6-terra | ultra, high | 36,430,562 |
| ... | 12 more | | | | | |

### Prompts

- `019fb9cd` goal: complete the work defined cfd_solver_agentic_benchmark/.
  - initial: /goal complete the work defined cfd_solver_agentic_benchmark/.
  - resume: /goal complete the work defined cfd_solver_agentic_benchmark/.
  - resume: /goal complete the work defined cfd_solver_agentic_benchmark/.
  - resume: /goal resume
  - resume: /goal resume
- `019fb9d5` goal: complete the work defined cfd_solver_agentic_benchmark/.
  - initial: /goal complete the work defined cfd_solver_agentic_benchmark/.
- `019fb9df` goal: complete the work defined cfd_solver_agentic_benchmark/.
  - initial: /goal complete the work defined cfd_solver_agentic_benchmark/.
  - resume: /goal complete the work defined cfd_solver_agentic_benchmark/.
- `019fb9e3` goal: complete the work defined cfd_solver_agentic_benchmark/.
  - initial: /goal complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fb9e3` | _command({cmd:"python3 -m venv .venv && .venv/bin/python -m pip install --disable-pip-version-check -r requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cod |
| medium | network_access | `019fba34` | \n+\n+```bash\n+python3 -m venv .venv\n+.venv/bin/python -m pip install --upgrade pip\n+.venv/bin/python -m pip install -r requirements.txt\n+```\n+\n+## Configure, build, and test\n+\n+The f |

## Reviews

- Code review scorecard: `codex_gpt56_01/review_code.md` (overall: None)
- CFD methods review: `codex_gpt56_01/review_cfd.md` (overall: None)
- Result review: `codex_gpt56_01/review_results.md` (overall: None)

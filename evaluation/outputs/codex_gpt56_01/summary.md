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

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fb9e3` | _command({cmd:"python3 -m venv .venv && .venv/bin/python -m pip install --disable-pip-version-check -r requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cod |
| medium | network_access | `019fba34` | \n+\n+```bash\n+python3 -m venv .venv\n+.venv/bin/python -m pip install --upgrade pip\n+.venv/bin/python -m pip install -r requirements.txt\n+```\n+\n+## Configure, build, and test\n+\n+The f |

## Reviews

- Code review scorecard: `codex_gpt56_01/review_code.md` (overall: None)
- CFD methods review: `codex_gpt56_01/review_cfd.md` (overall: None)
- Result review: `codex_gpt56_01/review_results.md` (overall: None)

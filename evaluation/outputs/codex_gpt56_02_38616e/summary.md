# Final Result Summary — 02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/02`
- Branch: `codex/gpt56/02` commit `6183dc8fc067cd7dc4a4a2949ca53bd4f2b42820`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/02/solver/rank_validation`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/02/report`)
- Session window: 2026-08-01T11:47:10.535000+00:00 → 2026-08-03T19:38:04.163000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-01T11:47:10.535000+00:00 → 2026-08-03T19:38:04.163000+00:00; 112×1800s buckets; idle 11 gaps / 135753s excluded; permission-wait candidates 0; tokens 593,126,278 (cache hit 0.9731)

## Expenses

- Goal time (codex): **62777 s**
- Wall time: **201054 s**
- Tokens: **593,126,278** (main 516,918,658 / subagents 76,207,620)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fbd25` | complete | gpt-5.6-sol | 36 | 593,126,278 | 62777 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-luna | 15,015,227 | 11,732,480 | 64,389 | 15,079,616 |
| gpt-5.6-sol | 517,057,545 | 509,668,096 | 971,709 | 518,029,254 |
| gpt-5.6-terra | 59,498,622 | 54,278,400 | 518,786 | 60,017,408 |

- Cost estimate: **$366.68** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **3,687**; top tools: exec=2954, wait=452, wait_agent=145, send_message=55, spawn_agent=36, list_agents=24
- Subagent spawns: 35
- LOC (file scan): 10,311 lines / 26 files
- LOC (git tracked): 10,311 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 8c9f03983ec1 (matches HEAD)
- CodeGraph: absent
- Benchmark submodule: 1bc6580b8482 (dirty)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-luna | xhigh, high, medium | 353400 | 1,373,611 | 4 |
| gpt-5.6-sol | max, high | 353400 | 1,024,088,488 | 2 |
| gpt-5.6-terra | high, xhigh | 353400 | 11,607,504 | 30 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fbd26` | `019fbd25` | Linnaeus | spec_audit | gpt-5.6-terra | high | 1,477,271 |
| `019fbd27` | `019fbd25` | Poincare | examiner_audit | gpt-5.6-terra | high | 3,820,989 |
| `019fbd27` | `019fbd25` | Fermat | mesh_solver_audit | gpt-5.6-terra | high | 4,548,000 |
| `019fbd7d` | `019fbd25` | Bacon | anderson_review | gpt-5.6-terra | high | 1,481,101 |
| `019fbdb4` | `019fbd25` | James | report_upgrade | gpt-5.6-terra | high | 1,204,203 |
| `019fbdb7` | `019fbd25` | Newton | transient_audit | gpt-5.6-terra | high | 780,536 |
| `019fbdb7` | `019fbd25` | Darwin | viscous_audit | gpt-5.6-terra | high | 530,874 |
| `019fbdd2` | `019fbd25` | Hilbert | transient_patch_review | gpt-5.6-terra | high | 581,514 |
| `019fbdd3` | `019fbd25` | Dewey | report_review | gpt-5.6-terra | high | 8,234,156 |
| `019fbdd3` | `019fbd25` | Einstein | steady_convergence_review | gpt-5.6-terra | high | 484,151 |
| `019fbe3c` | `019fbd25` | Avicenna | transient_performance_review | gpt-5.6-terra | high | 515,841 |
| `019fbe99` | `019fbd25` | Faraday | physical_results_audit | gpt-5.6-sol | high, max | 1,110,596 |
| `019fbe99` | `019fbd25` | McClintock | re200_readiness_audit | gpt-5.6-terra | high | 1,018,302 |
| `019fbe9e` | `019fbd25` | Euler | supersonic_te_artifact_review | gpt-5.6-terra | xhigh | 5,721,221 |
| `019fbe9e` | `019fbe9e` | Kierkegaard | result_diagnostics | gpt-5.6-terra | xhigh, high | 1,030,527 |
| `019fbeb0` | `019fbd25` | Hume | exact_wall_code_review | gpt-5.6-terra | high | 423,262 |
| `019fbeb1` | `019fbd25` | Arendt | rubric_source_audit | gpt-5.6-terra | high | 1,226,610 |
| `019fbed8` | `019fbd25` | Tesla | te_second_opinion | gpt-5.6-terra | xhigh | 9,624,010 |
| `019fbed8` | `019fbed8` | Copernicus | mesh_audit | gpt-5.6-terra | high | 808,835 |
| `019fbf03` | `019fbe9e` | Banach | output_evidence_audit | gpt-5.6-terra | high | 695,522 |
| `019fc187` | `019fbd25` | Godel | m080_flux_audit | gpt-5.6-terra | xhigh | 2,294,421 |
| `019fc188` | `019fbd25` | Cicero | m080_symmetry_diagnostics | gpt-5.6-luna | xhigh | 6,330,475 |
| `019fc207` | `019fbd25` | Jason | transient_resume | gpt-5.6-terra | high | 2,362,487 |
| `019fc208` | `019fbd25` | Hubble | completion_audit | gpt-5.6-luna | high | 2,792,188 |
| `019fc216` | `019fbd25` | Lovelace | production_supervisor | gpt-5.6-terra | high | 1,131,791 |
| `019fc2a6` | `019fbd25` | Ramanujan | m200_field_diagnosis | gpt-5.6-terra | high | 3,309,498 |
| `019fc2a7` | `019fbd25` | Aristotle | m200_numerics_audit | gpt-5.6-luna | high | 4,578,403 |
| `019fc2ed` | `019fbd25` | Boole | re200_runtime_audit | gpt-5.6-terra | high | 1,409,472 |
| `019fc2f4` | `019fbd25` | Popper | anisotropic_cell_stabilization | gpt-5.6-terra | high | 1,064,885 |
| `019fc32c` | `019fbd25` | Franklin | remaining_requirements_audit | gpt-5.6-terra | high | 497,500 |
| ... | 5 more | | | | | |

### Prompts

- `019fbd25` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fbd25` |  python3 -m venv solver/.venv && solver/.venv/bin/python -m pip install -r solver/requirements.txt && solver/.venv/bin/python -m unittest discover -s solver/tests -p 'test_*.py' -v && solver/ |
| medium | network_access | `019fbd27` | nt only:\n+#   python3 -m venv .venv && .venv/bin/python -m pip install -r requirements.txt\n+numpy>=1.24\n+matplotlib>=3.7\n+pytest>=7.4\n*** Add File: /mnt/ssd-SATARAID5/harry/projects/cfd_ |
| medium | network_access | `019fbd27` | h\n+cd solver\n+python3 -m venv .venv\n+.venv/bin/python -m pip install -r requirements.txt\n+.venv/bin/python -m pytest -q tests\n+```\n+\n+The standard field interchange format is an ASCII  |
| medium | network_access | `019fbdd3` | h\n+cd solver\n+python3 -m venv .venv\n+.venv/bin/python -m pip install -r requirements.txt\n+.venv/bin/python -m pytest -q tests\n+cd ..\n+```\n+\n+## Production runs and restart\n+\n+Run ea |
| medium | sandbox_escalation | `019fbd25` | _ms:10000,   max_output_tokens:2000,   sandbox_permissions:"require_escalated",   justification:"May I inspect the exact user service supervising the long Re=200 CFD run so I can verify it is healt |

## Reviews

- Code review scorecard: `codex_gpt56_02_38616e/review_code.md` (overall: 4.85)
- CFD methods review: `codex_gpt56_02_38616e/review_cfd.md` (overall: 4.92)
- Result review: `codex_gpt56_02_38616e/review_results.md` (overall: 4.55)

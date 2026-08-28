# Final Result Summary — 04

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/04`
- Branch: `codex/gpt56/04` commit `fe320b995caf44acceee26f893cbc4dd355737b8`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/04/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/04/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/04/solver/report`)
- Session window: 2026-08-04T10:11:34.470000+00:00 → 2026-08-05T11:44:54.367000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-04T10:11:34.470000+00:00 → 2026-08-05T11:44:54.367000+00:00; 52×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 689,394,311 (cache hit 0.9745)

## Expenses

- Goal time: **91963 s**
- Wall time: **92000 s**
- Tokens: **689,394,311** (main 611,629,280 / subagents 77,765,031)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fcc41` | complete | gpt-5.6-terra | 27 | 689,394,311 | 91963 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-terra | 687,557,969 | 670,055,424 | 1,836,342 | 689,394,311 |

- Cost estimate: **$241.26** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **5,059**; top tools: exec=3675, wait=1118, send_message=110, wait_agent=49, followup_task=40, list_agents=38
- Subagent spawns: 26
- LOC (file scan): 6,585 lines / 18 files
- LOC (git tracked): 6,585 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai_vanilla)
- AGENTS.md: sha256 8c9f03983ec1 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-terra | max, medium, high | 258400 | 324,319,794,624 | 27 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fcc42` | `019fcc41` | Banach | requirements_audit | gpt-5.6-terra | max, medium | 416,907 |
| `019fcc42` | `019fcc41` | Ohm | solver_audit | gpt-5.6-terra | max, medium | 364,225 |
| `019fcc42` | `019fcc41` | Archimedes | validation_audit | gpt-5.6-terra | max, medium | 525,952 |
| `019fcc44` | `019fcc41` | Kierkegaard | report_tooling | gpt-5.6-terra | max, medium | 2,542,225 |
| `019fcc45` | `019fcc41` | Pascal | mesh_mpi_module | gpt-5.6-terra | max, medium | 2,487,140 |
| `019fcc4a` | `019fcc41` | Hubble | output_module | gpt-5.6-terra | max, medium | 12,741,071 |
| `019fcc70` | `019fcc41` | Aristotle | numerical_review | gpt-5.6-terra | max, medium | 31,342,069 |
| `019fcf67` | `019fcc41` | Herschel | high_mach_strategy | gpt-5.6-terra | max, high | 343,756,559 |
| `019fcfc4` | `019fcc41` | Meitner | solver_pathology | gpt-5.6-terra | max, medium | 364,975,709 |
| `019fcfd9` | `019fcc41` | Godel | boundary_audit | gpt-5.6-terra | max, high | 380,156,960 |
| `019fcfe8` | `019fcc41` | Pauli | viscous_audit | gpt-5.6-terra | max, high | 383,523,388 |
| `019fd022` | `019fcc41` | Ampere | m2_gradient_trial | gpt-5.6-terra | max, high | 419,254,516 |
| `019fd022` | `019fcc41` | Curie | m08_gradient_trial | gpt-5.6-terra | max, high | 419,386,297 |
| `019fd024` | `019fcc41` | Avicenna | m2_corrected_viscous_trial | gpt-5.6-terra | max, high | 420,026,722 |
| `019fd024` | `019fcc41` | Boyle | m08_corrected_viscous_trial | gpt-5.6-terra | max, high | 420,187,145 |
| `019fd037` | `019fcc41` | Sartre | outer_line_search_review | gpt-5.6-terra | max, high | 430,604,208 |
| `019fd044` | `019fcc41` | Beauvoir | m08_pcontinuation_p0 | gpt-5.6-terra | max, high | 439,009,221 |
| `019fd04a` | `019fcc41` | Volta | trailing_edge_flux_audit | gpt-5.6-terra | max, high | 443,819,939 |
| `019fd04a` | `019fcc41` | Fermat | viscous_discretization_review | gpt-5.6-terra | max, high | 443,214,531 |
| `019fd04e` | `019fcc41` | McClintock | implicit_energy_mode | gpt-5.6-terra | max, high | 447,866,571 |
| `019fd072` | `019fcc41` | Singer | mpi_preconditioner_review | gpt-5.6-terra | max, high | 465,383,414 |
| `019fd0c2` | `019fcc41` | Mendel | residual_root_audit | gpt-5.6-terra | max, medium | 501,787,014 |
| `019fd0d1` | `019fcc41` | Nash | convergence_qualification | gpt-5.6-terra | max, medium | 511,841,902 |
| `019fd125` | `019fcc41` | Hypatia | completion_requirements | gpt-5.6-terra | medium | 1,615,972 |
| `019fd189` | `019fcc41` | Chandrasekhar | inviscid_plateau_fix | gpt-5.6-terra | max, high | 600,606,381 |
| `019fd189` | `019fcc41` | Epicurus | debug_strategy_audit | gpt-5.6-terra | max, high | 599,588,686 |

### Prompts

- `019fcc41` goal: complete the work defined cfd_solver_agentic_benchmark/.
  - initial: o

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fcc41` | n+```bash\n+python3 -m venv solver/.venv\n+solver/.venv/bin/pip install -r solver/tools/requirements.txt\n+solver/.venv/bin/python solver/tools/generate_report.py --report solver/report \\\n+ |
| medium | network_access | `019fcc41` | mand({cmd:"python3 -m venv solver/.venv && solver/.venv/bin/pip install -r solver/tools/requirements.txt",workdir:"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04",yiel |
| medium | network_access | `019fcc44` | ment once:\n+\n+```bash\n+python3 -m venv .venv\n+.venv/bin/pip install -r solver/tools/requirements.txt\n+```\n+\n+After running the solver for all cases, generate a report directory.  The\n |
| medium | network_access | `019fcc44` | ment once:\n+\n+```bash\n+python3 -m venv .venv\n+.venv/bin/pip install -r solver/tools/requirements.txt\n+```\n+\n+After running the solver for all cases, generate a report directory. The\n+ |
| medium | network_access | `019fcc44` | tmp/report-tool-test-venv && /tmp/report-tool-test-venv/bin/pip install -q -r solver/tools/requirements.txt && /tmp/report-tool-test-venv/bin/python - <<'PY'\nimport meshio\nfrom pathlib impo |
| medium | network_access | `019fcc44` | tool-test-venv/bin/pip list; /tmp/report-tool-test-venv/bin/pip install -r solver/tools/requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04","y |
| medium | network_access | `019fcc4a` | const r = await tools.exec_command({"cmd":"python3 -m pip install --user -r solver/tools/requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56 |
| medium | network_access | `019fcc4a` | n3 -m venv /tmp/cfd-report-venv && /tmp/cfd-report-venv/bin/pip install -r solver/tools/requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04","y |
| medium | network_access | `019fcc4a` |  -m pip list | head -40; /tmp/cfd-report-venv/bin/python -m pip install -r solver/tools/requirements.txt","workdir":"/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_gpt56_04","y |

## Reviews

- Code review scorecard: `codex_gpt56_04_1e36f2/review_code.md` (overall: 3.9)
- CFD methods review: `codex_gpt56_04_1e36f2/review_cfd.md` (overall: 4.38)
- Result review: `codex_gpt56_04_1e36f2/review_results.md` (overall: 4.35)

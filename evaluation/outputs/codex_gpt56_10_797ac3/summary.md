# Final Result Summary — 10

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/10`
- Branch: `codex/gpt56/10` commit `02007d6f4ef575f88a24c33cfb05bb8ae9289b67`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/10/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/10/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/gpt56/10/solver/report`)
- Session window: 2026-08-17T06:10:24.334000+00:00 → 2026-08-26T06:32:43.758000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: accepted
- Session analysis: 2026-08-17T06:10:24.334000+00:00 → 2026-08-26T06:32:43.758000+00:00; 433×1800s buckets; idle 7 gaps / 187491s excluded; permission-wait candidates 0; tokens 6,126,004,780 (cache hit 0.9818)

## Expenses

- Goal time: **593568 s**
- Wall time: **778939 s**
- Tokens: **6,126,004,780** (main 5,822,652,463 / subagents 303,352,317)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a00e56` | complete | gpt-5.6-luna | 48 | 6,126,004,780 | 593568 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| gpt-5.6-luna | 5,834,333,738 | 5,738,615,424 | 5,380,053 | 5,839,713,791 |
| gpt-5.6-terra | 285,596,109 | 269,670,400 | 694,880 | 286,290,989 |

- Cost estimate: **$741.43** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **32,967**; top tools: exec=23028, wait=9179, send_message=301, wait_agent=185, list_agents=119, followup_task=102
- Subagent spawns: 47
- LOC (file scan): 8,159 lines / 13 files
- LOC (git tracked): 4,615 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai_vanilla)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 0a05c72c06d3 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| gpt-5.6-luna | max, medium | 272000 | 57,830,930,040 | 2 |
| gpt-5.6-terra | max, medium, high | 272000 | 14,040,017 | 46 |

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a00e57` | `01a00e56` | Kant | requirements_audit | gpt-5.6-luna | max, medium | 18,524,337 |
| `01a00e58` | `01a00e56` | Carver | repo_audit | gpt-5.6-terra | max, medium | 2,085,989 |
| `01a00e58` | `01a00e56` | Confucius | solver_strategy | gpt-5.6-terra | max, medium | 4,115,540 |
| `01a00f97` | `01a00e56` | Boyle | run_monitor | gpt-5.6-terra | max, medium | 216,400,576 |
| `01a00f97` | `01a00e56` | Descartes | report_audit | gpt-5.6-terra | max, medium | 216,142,757 |
| `01a00f98` | `01a00e56` | Kuhn | artifact_validator | gpt-5.6-terra | max, medium | 216,233,821 |
| `01a0102b` | `01a00e56` | Parfit | requirements_audit2 | gpt-5.6-terra | medium | 3,164,954 |
| `01a0102b` | `01a00e56` | Planck | solver_review2 | gpt-5.6-terra | medium | 15,813,386 |
| `01a0102b` | `01a00e56` | Newton | repro_audit2 | gpt-5.6-terra | medium | 11,382,039 |
| `01a012e6` | `01a00e56` | Nietzsche | convergence_design | gpt-5.6-terra | max, medium | 732,559,585 |
| `01a012e6` | `01a00e56` | Mendel | report_gapfix | gpt-5.6-terra | max, medium | 734,426,104 |
| `01a01400` | `01a00e56` | Huygens | re200_physics_audit | gpt-5.6-terra | max, high | 877,760,205 |
| `01a01401` | `01a00e56` | Dirac | completion_contract_audit | gpt-5.6-terra | max, high | 883,639,404 |
| `01a01496` | `01a00e56` | Socrates | transient_numerics | gpt-5.6-terra | max, high | 970,576,681 |
| `01a01496` | `01a00e56` | Avicenna | contract_completion | gpt-5.6-terra | max, high | 964,871,755 |
| `01a014ce` | `01a00e56` | Tesla | transient_strategy | gpt-5.6-terra | max, medium | 1,000,781,034 |
| `01a014ce` | `01a00e56` | Ohm | contract_status | gpt-5.6-terra | max, medium | 1,001,806,571 |
| `01a014ce` | `01a00e56` | Pascal | probe_analysis | gpt-5.6-terra | max, medium | 1,010,951,419 |
| `01a01a33` | `01a00e56` | Chandrasekhar | force_bug_audit | gpt-5.6-terra | max, medium | 1,431,287,764 |
| `01a01a33` | `01a00e56` | Zeno | artifact_validation | gpt-5.6-terra | max, medium | 1,430,277,220 |
| `01a01a50` | `01a00e56` | Archimedes | transient_review | gpt-5.6-terra | max, high | 1,444,510,860 |
| `01a01a71` | `01a00e56` | Darwin | verify_outputs | gpt-5.6-terra | max, medium | 1,472,540,274 |
| `01a01af8` | `01a00e56` | Hegel | re200_strategy | gpt-5.6-terra | max, medium | 1,531,264,804 |
| `01a01af8` | `01a00e56` | Lorentz | re200_profile | gpt-5.6-terra | max, medium | 1,530,930,248 |
| `01a01b64` | `01a00e56` | Russell | wake_candidate_audit | gpt-5.6-terra | medium | 682,919 |
| `01a01e28` | `01a00e56` | Sagan | algorithm_audit | gpt-5.6-terra | max, medium | 2,002,249,750 |
| `01a01e28` | `01a00e56` | Locke | diagnostic_scan | gpt-5.6-terra | max, medium | 2,014,214,821 |
| `01a01e62` | `01a00e56` | Popper | limiter_review | gpt-5.6-terra | max, medium | 2,027,187,405 |
| `01a0275e` | `01a00e56` | Epicurus | lowmach_strategy | gpt-5.6-terra | max, high | 2,327,912,379 |
| `01a0287a` | `01a00e56` | Mencius | req_audit | gpt-5.6-terra | max, high | 2,549,309,143 |
| ... | 17 more | | | | | |

### Prompts

- `01a00e56` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `01a00e56` | const r = await tools.exec_command({cmd:"git switch -c solver/cfd-benchmark",workdir:"/workspace",yield_time_ms:10000,max_output_tokens:10000}); text(r.output);  |
| medium | network_access | `01a00e56` | x || true; python3 -m venv solver/.venv && solver/.venv/bin/pip install -q numpy matplotlib",workdir:"/workspace",yield_time_ms:30000,max_output_tokens:30000}); text(r.output);  |
| medium | network_access | `01a00e56` | n+```bash\n+python3 -m venv solver/.venv\n+solver/.venv/bin/pip install numpy matplotlib\n+```\n+\n+## Run\n+\n+The required command is:\n+\n+```bash\n+mpirun -np 8 solver/build/cfd_solver so |
| medium | network_access | `01a00e56` | ols.exec_command({cmd:"python3 -m pip --version; python3 -m pip install --user numpy -q","workdir":"/workspace","yield_time_ms":30000,"max_output_tokens":4000}); text(r.output);  |
| medium | network_access | `01a00e56` | and({cmd:"test -d .venv || python3 -m venv .venv; .venv/bin/pip install numpy -q","workdir":"/workspace","yield_time_ms":30000,"max_output_tokens":4000}); text(r.output);  |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 35812 35815 35817 35821 35824 35827 35832 35833 35838 35841 35843 35848 2>/dev/null || true; ps -ef | rg 'cfd_solver|mpi |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 75317 2>/dev/null || true; ps -eo pid,cmd | rg 'probe_inv_high' | rg -v rg || true","workdir":"/workspace","yield_time_m |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 82715 82716 2>/dev/null || true; ps -eo pid,ppid,stat,cmd | rg '(probe_rusanov_scale01_500_new2|cfd_solver|mpirun)' | he |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 82821 82822 82823 82826 2>/dev/null || true; timeout 20s env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_GIT_REVISION=diagnostic  |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 82890 82891 82892 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 84483 84484 84485 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 84800 84801 84802 2>/dev/null || true; wc -l solver/results/probe_seed05_500_direct/residuals.csv","workdir":"/workspace |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 85265 85266 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r.output |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86168 86169 86170 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86366 86367 86368 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86590 86591 86592 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86692 86693 86694 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86791 86792 86793 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 86890 86891 86892 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87001 87002 87003 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87503 87504 87505 2>/dev/null || true; env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_LOW_MACH_PRECONDITION=1 CFD_SOLVER_LOW_MAC |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87550 87551 87552 2>/dev/null || true","workdir":"/workspace","yield_time_ms":10000,"max_output_tokens":10000}); text(r. |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87627 87628 2>/dev/null || true; env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_LOW_MACH_PRECONDITION=1 CFD_SOLVER_LOW_MACH_THET |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87715 87716 87717 2>/dev/null || true; env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_LOW_MACH_PRECONDITION=1 CFD_SOLVER_LOW_MAC |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87853 87854 87855 2>/dev/null || true; env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_LOW_MACH_PRECONDITION=1 CFD_SOLVER_LOW_MAC |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 87924 87925 87927 2>/dev/null || true; time timeout 120s env CFD_SOLVER_FLUX=rusanov CFD_SOLVER_LOW_MACH_PRECONDITION=1  |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 118082 118085 2>/dev/null || true; ps -eo pid,cmd | rg 'active_lmstab_theta(0.7|0.5)' | head","workdir":"/workspace","yi |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 118170 118175 2>/dev/null || true; ps -eo pid,cmd | rg 'active_lmstab_theta(0.7|0.5)' | head","workdir":"/workspace","yi |
| medium | suspicious_patterns | `01a00e56` | const r = await tools.exec_command({cmd:"kill -9 285933 2>/dev/null || true; ps -ef | rg 'pressuremass_rus_005_contact_relax01' || true",workdir:"/workspace",yield_time_ |
| medium | suspicious_patterns | `01a00e56` | 3 287406 287407 287408 287409 2>/dev/null || true; sleep 1; kill -9 287403 2>/dev/null || true; ps -ef | rg 'b01_restart1000' || true",workdir:"/workspace",yield_time_ms:10000,max_output_t |
| medium | suspicious_patterns | `01a00e56` | 0 288274 288276 288278 288281 2>/dev/null || true; sleep 1; kill -9 288264 2>/dev/null || true; ps -ef | rg 'pressuremass_rus_001_b01_wall1_3000' || true",workdir:"/workspace",yield_time_m |
| medium | suspicious_patterns | `01a00e56` | 8 288970 288972 288974 288978 2>/dev/null || true; sleep 1; kill -9 288960 2>/dev/null || true; ps -ef | rg 'energyfull_restart1000' || true",workdir:"/workspace",yield_time_ms:10000,max_o |
| medium | suspicious_patterns | `01a00e56` | 3 296726 296727 296728 296729 2>/dev/null || true; sleep 1; kill -9 296723 2>/dev/null || true; ps -ef | rg 'mass_b01_p005_wall1' || true",workdir:"/workspace",yield_time_ms:10000,max_outp |
| medium | suspicious_patterns | `01a00e56` | 3 297064 297065 297067 297070 2>/dev/null || true; sleep 1; kill -9 297053 2>/dev/null || true; ps -ef | rg 'coupled_restart1000' || true",workdir:"/workspace",yield_time_ms:10000,max_outp |
| medium | suspicious_patterns | `01a00e56` | 6 297239 297241 297246 297250 2>/dev/null || true; sleep 1; kill -9 297230 2>/dev/null || true; ps -ef | rg 'coupled_3000_np8' || true",workdir:"/workspace",yield_time_ms:10000,max_output_ |
| medium | suspicious_patterns | `01a014ce` | const r = await tools.exec_command({"cmd":"kill -9 123189 123193 2>/dev/null || true; ps -eo pid,cmd | rg 'probe_wallfirst_rus' || true","workdir":"/workspace","yield_time |
| medium | sandbox_escalation | `01a00e56` | ":"/workspace","yield_time_ms":10000,"sandbox_permissions":"require_escalated","justification":"The benchmark source and validation fixes are complete; may I stage the five tracked files so I can c |
| medium | sandbox_escalation | `01a00e56` | ":"/workspace","yield_time_ms":30000,"sandbox_permissions":"require_escalated","justification":"The staged solver and validation changes are ready as a local checkpoint; may I create the requested  |
| medium | sandbox_escalation | `01a00e56` | e_ms":1000,"max_output_tokens":12000,"sandbox_permissions":"require_escalated","justification":"The source is clean and the benchmark launcher is ready; may I run the full eight-case MPI production |
| medium | sandbox_escalation | `01a00e56` | ":"/workspace","yield_time_ms":30000,"sandbox_permissions":"require_escalated","justification":"The production Re=20 launcher needs the validated tiny relaxation and warm-up guard; may I checkpoint |
| ... | 165 more | | |

## Reviews

- Code review scorecard: `codex_gpt56_10_797ac3/review_code.md` (overall: 4.1)
- CFD methods review: `codex_gpt56_10_797ac3/review_cfd.md` (overall: 4.72)
- Result review: `codex_gpt56_10_797ac3/review_results.md` (overall: 3.85)

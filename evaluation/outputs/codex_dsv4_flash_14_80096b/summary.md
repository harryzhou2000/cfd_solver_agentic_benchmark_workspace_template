# Final Result Summary — 14

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/14`
- Branch: `codex/dsv4_flash/14` commit `05261f847a01729953e3d05caeaf4544c724d77f`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/14/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/14/solver/analysis/tmp_audit`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/14/solver/report`)
- Session window: 2026-08-16T18:11:45.163000+00:00 → 2026-08-18T08:25:31.656000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Session analysis: 2026-08-16T18:11:45.163000+00:00 → 2026-08-18T08:25:31.656000+00:00; 77×1800s buckets; idle 3 gaps / 42298s excluded; permission-wait candidates 0; tokens 1,653,739,626 (cache hit 0.9955)

## Expenses

- Goal time: **95297 s**
- Wall time: **137626 s**
- Tokens: **1,653,739,626** (main 1,051,317,722 / subagents 602,421,904)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a00bc5` | complete | BLSC/DeepSeek-V4-Pro-0813 | 11 | 1,653,739,626 | 95297 |
| `01a0106b` | None | deepseek/deepseek-v4-flash | 1 | 278,237,151 | 0 |
| `01a00f87` | None | deepseek/deepseek-v4-flash | 1 | 96,838,237 | 0 |
| `01a010f9` | None | deepseek/deepseek-v4-flash | 1 | 73,559,572 | 0 |
| `01a012e6` | None | deepseek/deepseek-v4-flash | 1 | 62,598,349 | 0 |
| `01a0106c` | None | deepseek/deepseek-v4-flash | 1 | 25,189,231 | 0 |
| `01a0120d` | None | deepseek/deepseek-v4-flash | 1 | 19,417,618 | 0 |
| `01a01396` | None | deepseek/deepseek-v4-flash | 1 | 16,239,805 | 0 |
| `01a00e71` | None | deepseek/deepseek-v4-flash | 1 | 16,117,536 | 0 |
| `01a013cc` | None | deepseek/deepseek-v4-flash | 1 | 7,735,759 | 0 |
| `01a013cc` | None | deepseek/deepseek-v4-flash | 1 | 6,488,646 | 0 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/DeepSeek-V4-Pro-0813 | 1,049,309,393 | 1,043,837,952 | 2,008,329 | 1,051,317,722 |
| deepseek/deepseek-v4-flash | 601,214,798 | 599,281,792 | 1,207,106 | 602,421,904 |

- Cost estimate: **unavailable** (unpriced tokens: 1,051,317,722)

## Measurements

- Tool calls: **5,251**; top tools: exec=4630, wait=346, write_stdin=105, exec_command=88, wait_agent=43, list_agents=18
- Subagent spawns: 10
- LOC (file scan): 17,471 lines / 95 files
- LOC (git tracked): 7,865 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 10fe7826ce35 (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/DeepSeek-V4-Pro-0813 | ultra | 550000 | 263,532,840,000 | 1 |
| deepseek/deepseek-v4-flash | high | 550000 | 96,635,412 | 10 |

### opencodex router (non-vanilla models: BLSC/DeepSeek-V4-Pro-0813, deepseek/deepseek-v4-flash)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "openai", "opencode-free"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "BLSC/MiniMax-M3", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSeek-V3-250324", "BLSC/DeepSeek-R1-0528", "BL

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a00e71` | `01a00bc5` | Plato | stability_investigation | deepseek/deepseek-v4-flash | high | 16,117,536 |
| `01a00f87` | `01a00bc5` | Rawls | jacobian_audit | deepseek/deepseek-v4-flash | high | 96,838,237 |
| `01a0106b` | `01a00bc5` | James | steady_investigation | deepseek/deepseek-v4-flash | high | 278,237,151 |
| `01a0106c` | `01a00bc5` | Bernoulli | re200_investigation | deepseek/deepseek-v4-flash | high | 25,189,231 |
| `01a010f9` | `01a00bc5` | Lagrange | limit_cycle_audit | deepseek/deepseek-v4-flash | high | 73,559,572 |
| `01a0120d` | `01a00bc5` | Aquinas | re200_damping_audit | deepseek/deepseek-v4-flash | high | 19,417,618 |
| `01a012e6` | `01a00bc5` | Bohr | steady_secondorder_cycle_audit | deepseek/deepseek-v4-flash | high | 62,598,349 |
| `01a01396` | `01a00bc5` | Harvey | final_deliverables_review | deepseek/deepseek-v4-flash | high | 16,239,805 |
| `01a013cc` | `01a00bc5` | Archimedes | source_rubric_audit | deepseek/deepseek-v4-flash | high | 7,735,759 |
| `01a013cc` | `01a00bc5` | Kuhn | build_cli_verify | deepseek/deepseek-v4-flash | high | 6,488,646 |

### Prompts

- `01a00bc5` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
  - resume: /goal resume
- `01a00e71` goal: none
- `01a00f87` goal: none
- `01a0106b` goal: none
- `01a0106c` goal: none
- `01a010f9` goal: none
- `01a0120d` goal: none
- `01a012e6` goal: none
- `01a01396` goal: none
- `01a013cc` goal: none
- `01a013cc` goal: none

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `01a00bc5` | ing"} ]}); await tools.exec_command({cmd: "cd /workspace && git switch -c solver/attempt 2>&1 && mkdir -p solver/src solver/tools solver/report/figures solver/results solver/build && ls solve |
| medium | network_access | `01a00bc5` | "cd /workspace/solver && python3 -m venv .venv && .venv/bin/pip install -q numpy matplotlib 2>&1 | tail -2; .venv/bin/python -c 'import numpy, matplotlib; print(numpy.__version__, matplotlib. |
| medium | network_access | `01a00bc5` | Python environment      python3 -m venv .venv     .venv/bin/pip install numpy matplotlib     .venv/bin/python tools/plot_case.py <result-dir> <case-id> report/figures <kind>     .venv/bin/pyt |
| medium | network_access | `01a00bc5` | Python environment      python3 -m venv .venv     .venv/bin/pip install numpy matplotlib     .venv/bin/python tools/plot_case.py <result-dir> <case-id> report/figures <kind>     .venv/bin/pyt |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: "pkill -9 -f smoke_naca_laminar_600; sleep 1; cd /workspace/solver && which gdb || apt list --installed 2>/dev/null | grep -i gdb  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: "pkill -9 -f smoke_naca_laminar_600; sleep 1; cd /workspace/solver && ps aux | grep cfd_solver | grep -v grep | wc -l"}); text(r.o |
| medium | suspicious_patterns | `01a00bc5` |  = await tools.exec_command({cmd: "cd /workspace/solver && pkill -9 -f 'solve --case tests/smoke_naca' 2>/dev/null; sleep 1; ps aux | grep cfd_solver | grep -v grep | wc -l"}); text(r.outp |
| medium | suspicious_patterns | `01a00bc5` |  = await tools.exec_command({cmd: "cd /workspace/solver && pkill -9 -f 'smoke_naca_laminar_600.json' 2>/dev/null; sleep 1; CFD_DEBUG=1 CFD_DEBUG_TRACE=1 mpirun --allow-run-as-root -np 1 ./ |
| medium | suspicious_patterns | `01a00bc5` | with pkill patterns that match your own command line; use 'pkill -9 -x cfd_solver' if you must kill.\n\nRequired experiments (run each to completion and record the results):\nE1 cylinder_m |
| medium | suspicious_patterns | `01a00bc5` | t_tokens": 1000} const r = await tools.exec_command({cmd: "pkill -9 -x cfd_solver; sleep 1; ps aux | grep cfd_solver | grep -v grep | wc -l", workdir: "/workspace/solver"}); text(r.output) |
| medium | suspicious_patterns | `01a00bc5` | t_tokens": 1000} const r = await tools.exec_command({cmd: "pkill -9 -x cfd_solver; sleep 1; cd /workspace/solver && mkdir -p results/fix3_e5 && ./build/cfd_solver solve --case tests/e5_nac |
| medium | suspicious_patterns | `01a00bc5` | t_tokens": 1000} const r = await tools.exec_command({cmd: "pkill -9 -x cfd_solver; sleep 1; cd /workspace/solver && /workspace/solver/.venv/bin/python - <<'EOF'\nimport json\nbase='/worksp |
| medium | suspicious_patterns | `01a00bc5` | t_tokens": 4000} const r = await tools.exec_command({cmd: "pkill -9 -x cfd_solver; cd /workspace/solver && /workspace/solver/.venv/bin/python - <<'EOF'\nimport sys, numpy as np\nsys.path.i |
| medium | suspicious_patterns | `01a00bc5` | 35103 35106 2>/dev/null; sleep 2; ps -p 35106 >/dev/null && kill -9 35106; echo killed; cd /workspace/solver && nohup bash -c 'env -u DISPLAY timeout 10800 mpirun --allow-run-as-root -np 1 |
| medium | suspicious_patterns | `01a00bc5` | 1 35142 35145 37573 37574 37575 37578 2>/dev/null; sleep 1; kill -9 35145 37578 2>/dev/null; echo done; ps aux | grep cfd_solver | grep -v grep | wc -l", workdir: "/workspace"}); text(JSON |
| medium | suspicious_patterns | `01a00bc5` | command({cmd: "kill 40588 40589 40594 2>/dev/null; sleep 1; kill -9 40594 2>/dev/null; echo killed; cd /workspace/solver && nohup bash -c 'env -u DISPLAY timeout 2400 mpirun --allow-run-as |
| medium | suspicious_patterns | `01a00bc5` | command({cmd: "kill 42977 42978 42983 2>/dev/null; sleep 1; kill -9 42983 2>/dev/null; cd /workspace/solver && timeout 90 env -u DISPLAY CFD_DEBUG=1 mpirun --allow-run-as-root -np 1 ./buil |
| medium | suspicious_patterns | `01a00bc5` | command({cmd: "kill 43000 43001 43006 2>/dev/null; sleep 1; kill -9 43006 2>/dev/null; cd /workspace/solver && nohup bash -c 'env -u DISPLAY CFD_NO_STEADY_FREEZE=1 timeout 2400 mpirun --al |
| medium | suspicious_patterns | `01a00bc5` | command({cmd: "kill 44066 44067 44072 2>/dev/null; sleep 1; kill -9 44072 2>/dev/null; cd /workspace/solver && nohup bash -c 'env -u DISPLAY CFD_NO_STEADY_FREEZE=1 CFD_LIMITER=barth timeou |
| medium | suspicious_patterns | `01a00bc5` | 5 54959 54960 54965 54972 54973 54976 2>/dev/null; sleep 2; kill -9 49460 51145 54965 54976 2>/dev/null; cd /workspace/solver && cmake --build build -j 8 2>&1 | tail -2", workdir: "/worksp |
| medium | suspicious_patterns | `01a00bc5` | p 'cfd_solver solve' | grep -v grep | awk '{print $2}'); do kill -9 $p 2>/dev/null; done; echo cleaned; ps aux | grep cfd_solver | grep -v grep | wc -l", workdir: "/workspace"}); text(JSON |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 't_re200_diag_c.json'; sleep 2; ps aux | grep -E 't_re200_diag' | grep -v grep | grep -oE 'diag_[a-d]' | sort | uniq  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 'output results/naca0012_m080_laminar_re5000 --flux rusanov'; pkill -9 -f 'output results/cylinder_m010_laminar_re20  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `kill -9 59695 59698 59717 59720 59724 59731 59733 59741 59742 59747 2>/dev/null; sleep 3; ps -p 59717,59720 -o pid= 2>/dev/null  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 'output results/roe_m015_inv --flux roe' 2>/dev/null; ps aux | grep 'roe_m015_inv' | grep -v grep | awk '{print $2}'  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 't_re200_diag_d.json --output results/diag_e' 2>/dev/null; ps aux | grep 'diag_e' | grep -v grep | awk '{print $2}' | |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 'output results/diag_b --flux rusanov' 2>/dev/null; ps aux | grep 'diag_b' | grep -v grep | awk '{print $2}' | xargs  |
| medium | suspicious_patterns | `01a00bc5` | const r = await tools.exec_command({cmd: `pkill -9 -f 'output results/rankcmp_re20_np1' 2>/dev/null; ps aux | grep 'rankcmp_re20_np1' | grep -v grep | awk '{print $2}' | x |
| medium | suspicious_patterns | `01a00bc5` | exec_command({ cmd: "kill 70283 70286 2>/dev/null; sleep 4; kill -9 70283 70286 2>/dev/null; pkill -9 -f 'diag_g.json'; pkill -9 -f 'diag_j.json'; sleep 2; ps -eo args | grep -E 'diag_[gj] |
| medium | suspicious_patterns | `01a00bc5` |  "cd /workspace/solver && kill 70284 2>/dev/null; sleep 2; pkill -9 -f 'diag_h.json' 2>/dev/null; sleep 1; sed -e 's/t_re200_diag_i/t_re200_diag_k/' -e 's/venkat K=10, seed 0.02/venkat K=0 |
| medium | suspicious_patterns | `01a00bc5` | exec_command({ cmd: "kill 74529 74541 2>/dev/null; sleep 4; kill -9 74529 74541 2>/dev/null; ps -eo args | grep 'm015_inv_new' | grep -v grep | wc -l", yield_time_ms: 15000, max_output_tok |
| medium | suspicious_patterns | `01a00bc5` | /workspace/solver && pkill -f 'cfd_solver solve'; sleep 4; pkill -9 -f 'cfd_solver solve'; sleep 2; ps -eo args | grep -c cfd_solver; true", yield_time_ms: 20000, max_output_tokens: 4000 } |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m015_rs03'; sleep 1; ps -eo args | grep cfd_solver | grep -v grep | grep -oE 'results/[a-z0-9_]+' | sort | u |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m015_mf'; mkdir -p results/m015_rs08 results/re20_rs07; setsid bash -c 'CFD_RECON_SCALE=0.8 CFD_NO_WATCHDOG= |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/re20_default'; sleep 1; echo done`, workdir: '/workspace/solver', yield_time_ms: 30000}); text(r.output); |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m015_rs08'; pkill -9 -f 'results/re20_rs07'; sleep 1; echo done`, workdir: '/workspace/solver', yield_time_m |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 're20_rs07'; sleep 2; ps -eo args | grep cfd_solver | grep -v grep | grep -oE 'results/[a-z0-9_]+' | sort | uniq -c`, |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m080_lam_prod'; sleep 1; echo done`, workdir: '/workspace/solver', yield_time_ms: 30000}); text(r.output); |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m080_lam_wd'; sleep 1; mkdir -p results/m080_lam_pin; setsid bash -c 'CFD_STEADY_CFL=10 CFD_NO_WATCHDOG=1 mp |
| medium | suspicious_patterns | `01a00bc5` | time_ms": 30000} const r = await tools.exec_command({cmd: `pkill -9 -f 'results/m080_lam_pin'; sleep 1; echo done`, workdir: '/workspace/solver', yield_time_ms: 30000}); text(r.output); |
| ... | 13 more | | |

## Reviews

- Code review scorecard: `codex_dsv4_flash_14_80096b/review_code.md` (overall: 4.31)
- CFD methods review: `codex_dsv4_flash_14_80096b/review_cfd.md` (overall: 4.36)
- Result review: `codex_dsv4_flash_14_80096b/review_results.md` (overall: 4.38)

# Final Result Summary — 07

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/07`
- Branch: `codex/kimik3/07` commit `e55282a56daec8daa7af868e851ae4d0d7f8d9e2`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/07/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/07/.trash`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/07/solver/report`)
- Session window: 2026-08-26T09:31:36.565000+00:00 → 2026-08-26T16:37:23.338000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-26T09:31:36.565000+00:00 → 2026-08-26T16:37:23.338000+00:00; 15×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 160,579,799 (cache hit 0.9753)

## Expenses

- Goal time: **25525 s**
- Wall time: **25547 s**
- Tokens: **160,579,799** (main 157,627,779 / subagents 2,952,020)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a03d68` | complete | internal_eccn/nvidia-moonshotai-eccn-kimi-k3 | 2 | 160,579,799 | 25525 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| internal_eccn/nvidia-moonshotai-eccn-kimi-k3 | 160,032,589 | 156,075,791 | 547,210 | 160,579,799 |

- Cost estimate: **unavailable** (unpriced tokens: 160,579,799)

## Measurements

- Tool calls: **765**; top tools: exec=750, wait=14, spawn_agent=1
- Subagent spawns: 1
- LOC (file scan): 992,579 lines / 1844 files
- LOC (git tracked): 6,034 lines

## Metadata

- Harness: codex cli 0.149.1 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 8fee0d6b87d5 (differs from HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| internal_eccn/nvidia-moonshotai-eccn-kimi-k3 | max | 550000 | 2,898,576 | 2 |

### opencodex router (non-vanilla models: internal_eccn/nvidia-moonshotai-eccn-kimi-k3)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "internal_eccn", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a03db0` | `01a03d68` | Darwin | plot_tools | internal_eccn/nvidia-moonshotai-eccn-kimi-k3 | max | 2,952,020 |

### Prompts

- `01a03d68` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Com
  - resume: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `01a03d68` | r_venv_test/bin 2>/dev/null | head -3; solver_venv_test/bin/pip install --quiet numpy matplotlib 2>&1 | tail -3 && solver_venv_test/bin/python -c 'import numpy, matplotlib; print(\"pip ok\",  |
| medium | network_access | `01a03d68` | N ENV: create /workspace/solver/.venv (python3 -m venv) and pip install numpy matplotlib (already verified to work). All scripts must be runnable as /workspace/solver/.venv/bin/python tools/< |
| medium | network_access | `01a03d68` | r tools:  ```bash cd solver python3 -m venv .venv .venv/bin/pip install numpy matplotlib # then e.g. .venv/bin/python tools/plot_results.py --case-dir results/<case> --case-id <case> --out re |
| medium | network_access | `01a03d68` |  @BT@@BT@@BT@bash cd solver python3 -m venv .venv .venv/bin/pip install numpy matplotlib # then e.g. .venv/bin/python tools/plot_results.py --case-dir results/<case> --case-id <case> --out re |
| medium | network_access | `01a03db0` | venv /workspace/solver/.venv && /workspace/solver/.venv/bin/pip install -q numpy matplotlib && /workspace/solver/.venv/bin/python -c \"import numpy, matplotlib; print(numpy.__version__, matpl |
| high | unauthorized_remote_mutations | `01a03d68` | const r = await tools.exec_command({cmd: "cd /workspace && git checkout -b solver/kimi-k3-attempt1 2>&1 && mkdir -p solver/src solver/tools solver/report/figures solver/results && git status - |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "pkill -9 -f cfsolver; sleep 2; pgrep -af cfsolver || echo ALL_DEAD; cd /workspace && mv solver/results/_smoke_naca .trash/smoke2_ |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1; cd /workspace && timeout 100 mpirun -np 1 solver/build/cfsolver solve --case c |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1; cd /workspace && timeout -s 9 45 mpirun -np 2 solver/build/cfsolver solve --ca |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "cd /workspace && pkill -9 -x cfsolver; sleep 1; head -30 .trash/re20.log", workdir: "/workspace", max_output_tokens: 3000}); text(r.output); |
| medium | suspicious_patterns | `01a03d68` | pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1 const P = (path, o, n) => {   let p = '*** Begin Patch\n*** Update File: ' + pa |
| medium | suspicious_patterns | `01a03d68` |     }       }`); const r = await tools.exec_command({cmd: "pkill -9 -x cfsolver 2>/dev/null; pkill -9 -x mpirun 2>/dev/null; sleep 1; cd /workspace/solver/build && make -j32 2>&1 | grep -E |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "cd /workspace && pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 2; pgrep -c cfsolver || echo ALL_DEAD; for c in naca0012_m015_inviscid naca0012_m |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "cd /workspace && pkill -9 -f re200_lmfix; sleep 1; CFD_DEBUG_INNER=1 timeout -s 9 200 mpirun -np 8 solver/build/cfsolver solve --case cfd_solver_a |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1; cd /workspace && CFD_DEBUG_INNER=1 timeout -s 9 150 mpirun -np 8 solver/build/ |
| medium | suspicious_patterns | `01a03d68` | ver/build && make -j32 2>&1 | grep -E 'error'; echo BUILT; pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1; cd /workspace && timeout -s 9 200 mpirun -np 8 solver/build/cfsolver solve --c |
| medium | suspicious_patterns | `01a03d68` | const r = await tools.exec_command({cmd: "pkill -9 -x cfsolver; pkill -9 -x mpirun; sleep 1; cd /workspace && CFD_DEBUG_INNER=1 timeout -s 9 100 mpirun -np 8 solver/build/ |
| medium | suspicious_patterns | `01a03d68` | pkill -9 -x cfsolver 2>/dev/null; true const P = (path, o, n) => {   let p = '*** Begin Patch\n*** Update File: ' + path + '\n@@\ |

## Reviews

- Code review scorecard: `codex_kimik3_07_f42559/review_code.md` (overall: 3.95)
- CFD methods review: `codex_kimik3_07_f42559/review_cfd.md` (overall: 3.91)
- Result review: `codex_kimik3_07_f42559/review_results.md` (overall: 3.7)

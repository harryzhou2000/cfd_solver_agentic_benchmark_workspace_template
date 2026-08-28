# Final Result Summary — vllm-qwen38-02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-02`
- Branch: `codex/generic/vllm-qwen38-02` commit `84ccf503fa1dcb716fbbf830f6645288a488bb9b`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-02/solver`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/vllm-qwen38-02/solver/report`)
- Session window: 2026-08-22T05:40:14.992000+00:00 → 2026-08-25T13:08:45.197000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-22T05:40:14.992000+00:00 → 2026-08-25T13:08:45.197000+00:00; 159×1800s buckets; idle 26 gaps / 109278s excluded; permission-wait candidates 0; tokens 455,190,499 (cache hit 0.9608)

## Expenses

- Goal time: **222054 s**
- Wall time: **286110 s**
- Tokens: **455,190,499** (main 336,955,344 / subagents 118,235,155)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a027fa` | complete | vllm/qwen3.8-27b-int8-w8a16-mtp | 10 | 455,190,499 | 222054 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| vllm/qwen3.8-27b-int8-w8a16-mtp | 449,519,316 | 431,910,864 | 5,671,183 | 455,190,499 |

- Cost estimate: **unavailable** (unpriced tokens: 455,190,499)

## Measurements

- Tool calls: **3,982**; top tools: exec=3623, exec_command=141, wait=49, wait_agent=36, apply_patch=35, send_message=34
- Subagent spawns: 9
- LOC (file scan): 12,032 lines / 108 files
- LOC (git tracked): 9,246 lines

## Metadata

- Harness: codex cli 0.148.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 7aa6dde4c59a (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (dirty)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| vllm/qwen3.8-27b-int8-w8a16-mtp | ultra, medium | 256000 | 947,603,490 | 10 |

### opencodex router (non-vanilla models: vllm/qwen3.8-27b-int8-w8a16-mtp)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-R1", "BLSC/DeepSe

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a02d4b` | `01a027fa` | Herschel | code_audit | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 10,891,864 |
| `01a02d4b` | `01a027fa` | Bohr | python_tools | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 12,787,765 |
| `01a02db3` | `01a02d4b` | Beauvoir | tools_review | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 1,690,752 |
| `01a03415` | `01a027fa` | Mill | plateau_stop | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 3,283,017 |
| `01a0343f` | `01a027fa` | Euclid | report_boilerplate | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 23,746,114 |
| `01a034bf` | `01a027fa` | Singer | re200_inner | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 41,960,289 |
| `01a0377a` | `01a027fa` | Gauss | re200_wake_monitor | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 2,053,509 |
| `01a03781` | `01a027fa` | Popper | citation_fix | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 4,848,268 |
| `01a037b3` | `01a027fa` | Hilbert | citation_fix2 | vllm/qwen3.8-27b-int8-w8a16-mtp | medium | 16,973,577 |

### Prompts

- `01a027fa` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .
  - resume: /goal resume
  - resume: /goal resume

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `01a027fa` | d /workspace/solver && python3 -m venv .venv && ./.venv/bin/pip install -q numpy matplotlib h5py 2>&1 | tail -2; ./.venv/bin/python -c \"import numpy, matplotlib, h5py; print('venv ok', numpy |
| medium | network_access | `01a027fa` | space/solver && ./.venv/bin/pip --version 2>&1; ./.venv/bin/pip install numpy matplotlib h5py 2>&1 | tail -15"}); text(r.output); |
| medium | network_access | `01a027fa` | ols.exec_command({cmd: "cd /workspace/solver && ./.venv/bin/pip install -v numpy 2>&1 | head -30; echo EXIT=$?"}); text(r.output); |
| medium | network_access | `01a027fa` | ols.exec_command({cmd: "cd /workspace/solver && ./.venv/bin/pip install matplotlib h5py 2>&1 | tail -5; ./.venv/bin/pip list 2>/dev/null | head -20; ./.venv/bin/python -c \"import numpy, matp |
| medium | network_access | `01a027fa` |  'import numpy; print(numpy.__version__)' 2>&1 | tail -1 && pip install scipy -q 2>&1 | tail -1; .venv/bin/pip install scipy -q 2>&1 | tail -2"}); text(r.output); |
| medium | network_access | `01a027fa` | /workspace/solver +    python3 -m venv .venv +    .venv/bin/pip install numpy matplotlib + +Regenerate the report package (order matters): + +    .venv/bin/python tools/plot_all.py \ +        |
| medium | network_access | `01a027fa` | d /workspace/solver     python3 -m venv .venv     .venv/bin/pip install numpy matplotlib  Regenerate the report package (order matters):      .venv/bin/python tools/plot_all.py \         --re |
| high | unauthorized_remote_mutations | `01a027fa` | const r = await tools.exec_command({cmd: "cd /workspace && git checkout -b solver/fv2d-cfd && git config user.name \"CFD Agent\" && git config user.email \"cfd-agent@workspace.local\" && mkdir |
| medium | suspicious_patterns | `01a027fa` | const r = await tools.exec_command({cmd: `kill -9 3160 3180 2>/dev/null; pkill -9 -f 'meshinfo --mesh' 2>/dev/null; sleep 1; ps aux | grep cfd_solver | grep -v grep | wc  |
| medium | suspicious_patterns | `01a027fa` | aux | grep -E 'cfd_solver|mpirun' | grep -v grep | head -5; kill -9 7223 7228 7229 2>/dev/null; sleep 1; ps aux | grep -E 'cfd_solver|mpirun' | grep -v grep | head -5; echo KILLED; cd /wor |
| medium | suspicious_patterns | `01a027fa` | = await tools.exec_command({ cmd: 'cd /workspace/solver && pkill -9 -f "cfd_solver solve" ; sleep 1; ps aux | grep cfd_solver | grep -v grep | wc -l; grep -n "steady" src/driver.cpp | head |
| medium | suspicious_patterns | `01a027fa` | r = await tools.exec_command({cmd: "cd /workspace/solver && kill -9 37246 37247 2>/dev/null; sleep 0.5; tail -1 st_inv_ramp/residuals.csv | awk -F, '{print \"ramp last:\", $1, $4, $10}'; w |
| medium | suspicious_patterns | `01a027fa` | p | grep 'lcf_full' | awk '{print $1}'); for p in $PIDS; do kill -9 $p 2>/dev/null; done; sleep 0.5; for t in st_inv_b0v1e-7 st_inv_b0v1e-5 st_inv_b0v1e-3 st_inv_b0v1.0 rt_cyl_np4_300; do  |
| medium | suspicious_patterns | `01a027fa` |  const note = `  ## 10g-4 — 21:22 UTC: m200 relaunch RE-EVAL (not diverging — healthy slow force relaxation)  - @s112: cd FLAT since s89 (0.1001 +/- 1e-4 over 23 steps). cl creeping  |
| medium | suspicious_patterns | `01a027fa` | \"probe procs remaining: $n\"; if [ \"$n\" != \"0\" ]; then kill -9 112111 112113 112116 112117 112118 112122 2>/dev/null; sleep 2; ps -eo pid,args | grep -F -- '--output probe_re200_stag1 |
| medium | suspicious_patterns | `01a034bf` |  "cd /workspace/solver/tmp_re200_1d && echo '" + b64 + "' | base64 -d > exp1.py && wc -l exp1.py && timeout 280 python3 exp1.py 2>&1 | tail -10", yield_time_ms: 290000, max_output_tokens: 1 |

## Reviews

- Code review scorecard: `codex_generic_vllm-qwen38-02_d47362/review_code.md` (overall: 4.3)
- CFD methods review: `codex_generic_vllm-qwen38-02_d47362/review_cfd.md` (overall: 4.04)
- Result review: `codex_generic_vllm-qwen38-02_d47362/review_results.md` (overall: 3.92)

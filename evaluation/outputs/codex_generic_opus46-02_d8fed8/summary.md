# Final Result Summary — opus46-02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-02`
- Branch: `codex/generic/opus46-02` commit `1904149e1cfb41664bf41e7786f614a2ba6ca56e`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-02/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/generic/opus46-02/solver/report`)
- Session window: 2026-08-25T01:19:34.641000+00:00 → 2026-08-25T03:33:46.516000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-25T01:19:34.641000+00:00 → 2026-08-25T03:33:46.516000+00:00; 5×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 59,332,420 (cache hit 0.9832)

## Expenses

- Goal time: **7692 s**
- Wall time: **8052 s**
- Tokens: **59,332,420** (main 50,124,064 / subagents 9,208,356)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `01a0367d` | complete | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | 17 | 59,332,420 | 7692 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | 58,982,470 | 57,988,993 | 349,950 | 59,332,420 |

- Cost estimate: **unavailable** (unpriced tokens: 59,332,420)

## Measurements

- Tool calls: **518**; top tools: exec=444, wait=25, wait_agent=21, spawn_agent=18, interrupt_agent=6, send_message=2
- Subagent spawns: 16
- LOC (file scan): 4,050 lines / 41 files
- LOC (git tracked): 4,050 lines

## Metadata

- Harness: codex cli 0.148.0 (codex-tui, provider openai)
- Plugins: openai-curated-remote github, openai-curated-remote google-drive, openai-curated-remote openai-templates, openai-curated-remote outlook-calendar, openai-curated-remote outlook-email, openai-curated-remote plugin-management, openai-curated-remote slack, openai-curated-remote teams, openai-curated-remote workspace-agents
- AGENTS.md: sha256 7aa6dde4c59a (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 550000 | 58,987,412 | 17 |

### opencodex router (non-vanilla models: internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6)

- opencodex version: None (submodule None)
- config facts: {"default_provider": "deepseek", "providers": ["BLSC", "deepseek", "internal_eccn", "kimi-code", "ollama", "openai", "opencode-free", "vllm", "xiaomi-mimo"], "multi_agent_mode": "v2", "subagent_models": ["gpt-5.6-terra", "gpt-5.6-luna", "deepseek/deepseek-v4-flash", "BLSC/GLM-5.3", "kimi-code/k3"], "disabled_models": ["BLSC/Baichuan-M2", "BLSC/Baichuan-M3", "BLSC/Baichuan-M2-128K", "BLSC/DeepSeek-

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `01a03684` | `01a0367d` | Aquinas | solver_core_impl | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 1,162,293 |
| `01a03685` | `01a03684` | Copernicus | infra_files | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 762,438 |
| `01a03685` | `01a03684` | Euclid | mesh_partition_files | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 666,273 |
| `01a03689` | `01a03684` | Laplace | flux_recon_files | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 734,430 |
| `01a03689` | `01a03689` | Mendel | write_flux_inviscid | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 130,346 |
| `01a0368d` | `01a03684` | Hilbert | lusgs_timeint_files | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 511,996 |
| `01a0368e` | `01a03684` | Planck | solver_file | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 490,935 |
| `01a0369d` | `01a0367d` | Jason | solver_stability_fix | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 606,435 |
| `01a0369e` | `01a0369d` | Lovelace | fix_flux_inviscid | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 175,850 |
| `01a0369e` | `01a0369d` | Boole | fix_lusgs | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 206,590 |
| `01a036a0` | `01a0369d` | Rawls | fix_solver_cpp | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 482,232 |
| `01a036b2` | `01a0367d` | Tesla | run_cases_batch1 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 205,762 |
| `01a036b3` | `01a0367d` | Erdos | run_cases_batch2 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 138,627 |
| `01a036b7` | `01a0367d` | Helmholtz | run_all_cases | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 912,685 |
| `01a036c7` | `01a0367d` | Sagan | run_production | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 1,840,882 |
| `01a036e2` | `01a0367d` | Maxwell | run_cyl_re200 | internal_eccn/us-aws-anthropic-eccn-claude-opus-4-6 | ultra | 155,661 |

### Prompts

- `01a0367d` goal: Complete the task defined in cfd_solver_agentic_benchmark/ .
  - initial: /goal Complete the task defined in cfd_solver_agentic_benchmark/ .

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `01a0367d` | venv /workspace/solver/.venv && /workspace/solver/.venv/bin/pip install numpy matplotlib 2>&1 | tail -5",   workdir: "/workspace",   yield_time_ms: 60000 }); text(r.output);  |
| medium | network_access | `01a0367d` | nt  ```bash python3 -m venv .venv source .venv/bin/activate pip install numpy matplotlib ```  ## Post-Processing  ```bash .venv/bin/python tools/plot_results.py ```  ## Architecture  The solv |
| medium | network_access | `01a0367d` |     python3 -m venv .venv     source .venv/bin/activate     pip install numpy matplotlib     python tools/plot_results.py  ## Features  - Rusanov/Roe approximate Riemann solvers - LU-SGS impl |
| high | unauthorized_remote_mutations | `01a0367d` | _command({   cmd: `cd /workspace && git init . 2>/dev/null; git checkout -b solver/benchmark-attempt 2>/dev/null || git checkout solver/benchmark-attempt 2>/dev/null || true`,   workdir: "/work |
| high | unauthorized_remote_mutations | `01a0367d` | st r2 = await tools.exec_command({   cmd: `cd /workspace && git checkout -b solver/benchmark-v1 2>/dev/null || true`,   workdir: "/workspace" }); text(r2.output);  |
| medium | suspicious_patterns | `01a036b7` | successfully. const r = await tools.exec_command({   cmd: "pkill -9 -f cfd2d; sleep 1; pgrep -f cfd2d | wc -l",   workdir: "/workspace",   yield_time_ms: 10000 }); text("After kill -9: " + |
| medium | suspicious_patterns | `01a036c7` | wait tools.exec_command({   cmd: "pkill -f cfd2d; sleep 2; pkill -9 -f cfd2d; sleep 2; ps aux | grep cfd2d | grep -v grep | wc -l",   workdir: "/workspace",   yield_time_ms: 15000 });  //  |
| medium | suspicious_patterns | `01a036c7` | esses and re-run case 9 await tools.exec_command({   cmd: "pkill -9 -f cfd2d; sleep 3; ps aux | grep cfd2d | grep -v grep | wc -l",   workdir: "/workspace",   yield_time_ms: 10000 }); text |
| medium | suspicious_patterns | `01a036c7` | stale processes for it. await tools.exec_command({   cmd: "pkill -9 -f 'naca0012_m015_inviscid.*np8'",   workdir: "/workspace",   yield_time_ms: 5000 });  // Now run Case 10: cylinder_m010 |
| medium | suspicious_patterns | `01a036c7` |  remaining stale processes await tools.exec_command({cmd: "pkill -9 -f cfd2d 2>/dev/null; sleep 1", workdir: "/workspace", yield_time_ms: 5000});  // Final summary text(`ALL 10 CASES COMPL |

## Reviews

- Code review scorecard: `codex_generic_opus46-02_d8fed8/review_code.md` (overall: 3.6)
- CFD methods review: `codex_generic_opus46-02_d8fed8/review_cfd.md` (overall: 3.16)
- Result review: `codex_generic_opus46-02_d8fed8/review_results.md` (overall: 2.3)

# Final Result Summary — 01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/01`
- Branch: `codex/kimik3/01` commit `005d7d987066c8a9325c6c482c1c55a1e37b4c4d`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/01/solver/rankcount`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/kimik3/01/solver/report`)
- Session window: 2026-08-01T12:23:09.026000+00:00 → 2026-08-04T21:19:43.639000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: no
- Vendored report PDF: accepted
- Session analysis: 2026-08-03T14:28:02.871000+00:00 → 2026-08-04T21:19:43.639000+00:00; 62×1800s buckets; idle 4 gaps / 75311s excluded; permission-wait candidates 0; tokens 82,061,679 (cache hit 0.8212)

## Expenses

- Goal time: **36426 s**
- Wall time: **111101 s**
- Tokens: **82,061,679** (main 82,061,679 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fc805` | complete | BLSC/Kimi-K3 | 1 | 82,061,679 | 36426 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/Kimi-K3 | 81,752,330 | 67,137,507 | 309,349 | 82,061,679 |

- Cost estimate: **$32.64** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **447**; top tools: exec_command=289, apply_patch=116, write_stdin=34, update_plan=6, spawn_agent=1, update_goal=1
- Subagent spawns: 0
- LOC (file scan): 4,519 lines / 26 files
- LOC (git tracked): 4,519 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 c8f6feb8107e (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (clean)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/Kimi-K3 | ultra | 522500 | 261,253,440 | 1 |

### opencodex router (non-vanilla models: BLSC/Kimi-K3)

- opencodex version: None (submodule None)
- config facts: {}

### Prompts

- `019fc805` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| high | unauthorized_remote_mutations | `019fc805` | ID5/harry/projects/cfd_agentic_benchmark/codex_kimik3_01 && git checkout -b solver/kimik3-01 && git branch --show-current |
| medium | network_access | `019fc805` | results && python3 -m venv solver/.venv && solver/.venv/bin/pip install -q --upgrade pip 2>&1 | tail -1 |
| medium | network_access | `019fc805` | s/cfd_agentic_benchmark/codex_kimik3_01/solver && .venv/bin/pip install -q numpy matplotlib h5py scipy 2>&1 | tail -2; .venv/bin/python -c "import numpy, matplotlib, h5py, scipy; print('venv  |
| medium | network_access | `019fc805` | nt: + +```bash +cd solver +python3 -m venv .venv +.venv/bin/pip install numpy scipy matplotlib h5py +``` + +## Build + +```bash +cmake -S solver -B solver/build -DCFD_EXTERNALS_ROOT="$PWD/ext |
| medium | suspicious_patterns | `019fc805` | 45099 2245100 2245103 2245104 2>/dev/null || true; sleep 2; kill -9 2245099 2245100 2245103 2245104 2>/dev/null || true; ps -eo pid,args | rg 'naca0012_m080_laminar_re5000|naca0012_m200_la |
| medium | suspicious_patterns | `019fc805` | 24072 1224080 1224108 1224109 2>/dev/null || true; sleep 2; kill -9 1224072 1224080 1224108 1224109 2>/dev/null || true; ps -eo pid,etime,args | rg 'naca0012_m200_inviscid' | rg -v rg |

## Reviews

- Code review scorecard: `codex_kimik3_01_09309d/review_code.md` (overall: 4.0)
- CFD methods review: `codex_kimik3_01_09309d/review_cfd.md` (overall: 4.65)
- Result review: `codex_kimik3_01_09309d/review_results.md` (overall: 4.3)

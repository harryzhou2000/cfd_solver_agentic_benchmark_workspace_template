# Final Result Summary — 01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/01`
- Branch: `codex/dsv4_flash/01` commit `f6d8a1839a70c98d83a1e5baff9e2d6e111f7e2f`
- Benchmark submodule: `ffc314f7dd886d03e5c8172f44272f2f58f9c7cc`
- Layout: non_standard (solver: `None`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/dsv4_flash/01/cfd_solver/results/cylinder_m010_laminar_re200`, report: `None`)
- Session window: 2026-07-31T17:09:05.035000+00:00 → 2026-07-31T19:35:56.498000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: accepted
- Session analysis: 2026-07-31T17:09:05.035000+00:00 → 2026-07-31T19:35:56.498000+00:00; 5×1800s buckets; idle 0 gaps / 0s excluded; permission-wait candidates 0; tokens 126,215,871 (cache hit 0.0297)

## Expenses

- Goal time: **0 s**
- Wall time: **8812 s**
- Tokens: **126,215,871** (main 126,215,871 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fb926` | None | BLSC/DeepSeek-V4-Flash | 1 | 126,215,871 | 0 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/DeepSeek-V4-Flash | 126,163,415 | 3,740,928 | 52,456 | 126,215,871 |

- Cost estimate: **$17.27** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **556**; top tools: exec_command=433, write_stdin=81, apply_patch=36, update_plan=5, updatemp_plan=1
- Subagent spawns: 0
- LOC (file scan): 3,609 lines / 20 files
- LOC (git tracked): 3,609 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 7bcbffcafddf (matches HEAD)
- CodeGraph: absent
- Benchmark submodule: ffc314f7dd88 (dirty)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/DeepSeek-V4-Flash | medium | 522500 | 495,012 | 1 |

### opencodex router (non-vanilla models: BLSC/DeepSeek-V4-Flash)

- opencodex version: None (submodule None)
- config facts: {}

### Prompts

- `019fb926` goal: none

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | suspicious_patterns | `019fb926` | kill -9 2074358 2074359 2074347 2074343 2>/dev/null; sleep 1; ps -eo pid,etime,args | grep -E "cfd_solver solve.*np2" | grep -v  |
| medium | suspicious_patterns | `019fb926` | kill -9 2158741 2158742 2158734 2158732 2>/dev/null; sleep 1; ps -eo pid,args | grep -E "np2_short_test" | grep -v grep; echo "c |

## Reviews

- Code review scorecard: `codex_dsv4_flash_01_0c1996/review_code.md` (overall: 3.32)
- CFD methods review: `codex_dsv4_flash_01_0c1996/review_cfd.md` (overall: 3.34)
- Result review: `codex_dsv4_flash_01_0c1996/review_results.md` (overall: 1.35)

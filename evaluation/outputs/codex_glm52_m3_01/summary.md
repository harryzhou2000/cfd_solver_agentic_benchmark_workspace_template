# Final Result Summary — codex_glm52_m3_01

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_01`
- Branch: `codex/glm52-m3/init` commit `44ff3cb3726e35ab0623171b48b7ff8b285f8707`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_01/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_01/solver/results`, report: `None`)

## Expenses

- Goal time (codex): **3430 s**
- Wall time: **3561 s**
- Tokens: **22,319,057** (main 22,319,057 / subagents 0)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fbef6` | active | BLSC/GLM-5.2 | 1 | 22,240,938 | 3408 |
| `019fbef4` | paused | BLSC/GLM-5.2 | 1 | 78,119 | 22 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/GLM-5.2 | 0 | 0 | 0 | 22,319,057 |

- Cost estimate: **$15.62** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **185**; top tools: exec_command=179, apply_patch=5, update_plan=1
- Subagent spawns: 0
- LOC (file scan): 3,850 lines / 15 files
- LOC (git tracked): 4,610 lines

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fbef6` | python3 -m venv .venv && .venv/bin/pip install --quiet h5py numpy matplotlib scipy 2>&1 | tail -5 && echo "VENV READY" |
| medium | network_access | `019fbef6` | .venv/bin/pip install h5py 2>&1 | tail -3 |

## Reviews

- Code review scorecard: `codex_glm52_m3_01/review_code.md` (overall: None)
- CFD methods review: `codex_glm52_m3_01/review_cfd.md` (overall: None)
- Result review: `codex_glm52_m3_01/review_results.md` (overall: None)

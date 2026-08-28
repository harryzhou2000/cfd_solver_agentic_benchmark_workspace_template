# Final Result Summary — 02

- Workspace: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/02`
- Branch: `codex/glm52/02` commit `1e29ab3c455de3e293f0d1e45840136ba4e4ec08`
- Benchmark submodule: `1bc6580b84825037bbeac097ede1b2226d8185d1`
- Layout: standard (solver: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/02/solver`, results: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/02/solver/results`, report: `/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/cfd_solver_benchmark_workspace_template/workspace/codex/glm52/02/solver/report`)
- Session window: 2026-08-01T11:52:37.928000+00:00 → 2026-08-01T15:46:50.451000+00:00

## Snapshot

- Configs captured: yes · sessions analyzed: yes · env snapshot: yes · agent report/scores: yes / yes
- Vendored report PDF: accepted
- Session analysis: 2026-08-01T11:52:37.928000+00:00 → 2026-08-01T15:46:50.451000+00:00; 8×1800s buckets; idle 1 gaps / 4747s excluded; permission-wait candidates 0; tokens 87,815,105 (cache hit 0.9688)

## Expenses

- Goal time: **9296 s**
- Wall time: **14052 s**
- Tokens: **87,815,105** (main 84,670,603 / subagents 3,144,502)

### Per root session

| Root | Status | Model | Threads | Tokens | Goal time (s) |
|------|--------|-------|--------:|-------:|--------------:|
| `019fbd2b` | complete | BLSC/GLM-5.2 | 2 | 87,815,105 | 9296 |

| Model | Input | Cached | Output | Total |
|-------|------:|-------:|-------:|------:|
| BLSC/GLM-5.2 | 87,547,520 | 84,818,560 | 267,585 | 87,815,105 |

- Cost estimate: **$10.00** (estimate; unpriced tokens: 0)

## Measurements

- Tool calls: **533**; top tools: exec_command=366, apply_patch=102, write_stdin=59, update_plan=2, view_image=2, spawn_agent=1
- Subagent spawns: 1
- LOC (file scan): 4,753 lines / 26 files
- LOC (git tracked): 4,753 lines

## Metadata

- Harness: codex cli 0.146.0 (codex-tui, provider openai)
- AGENTS.md: sha256 e08812a6e0fd (matches HEAD)
- CodeGraph: present
- Benchmark submodule: 1bc6580b8482 (dirty)

| Model | Effort(s) | Context window | Max context used | Threads |
|-------|-----------|---------------:|-----------------:|--------:|
| BLSC/GLM-5.2 | max, medium | 522500 | 287,853 | 2 |

### opencodex router (non-vanilla models: BLSC/GLM-5.2)

- opencodex version: None (submodule None)
- config facts: {}

### Subagent threads

| Thread | Parent | Nickname | Type | Model | Effort | Tokens |
|--------|--------|----------|------|-------|--------|-------:|
| `019fbd68` | `019fbd2b` | Bernoulli | plotting_scripts | BLSC/GLM-5.2 | medium | 3,144,502 |

### Prompts

- `019fbd2b` goal: complete the work defined cfd_solver_agentic_benchmark/.

### Rule-violation candidates

| Severity | Category | Thread | Evidence |
|----------|----------|--------|----------|
| medium | network_access | `019fbd2b` | /results && cd solver && python3 -m venv .venv && .venv/bin/pip install --quiet numpy matplotlib h5py 2>&1 | tail -5; echo "EXIT: $?" |
| medium | network_access | `019fbd2b` | h cd solver python3 -m venv .venv source .venv/bin/activate pip install numpy matplotlib h5py ```  ## Run a Single Case  ```bash export LD_LIBRARY_PATH=<workspace>/external/cfd_externals/inst |
| high | unauthorized_remote_mutations | `019fbd2b` | AID5/harry/projects/cfd_agentic_benchmark/codex_glm52_02 && git checkout -b solver/glm52/cfd2d 2>&1; git add solver/ .gitignore 2>/dev/null; git status --short | head -20 |
| medium | suspicious_patterns | `019fbd2b` | pkill -9 -f "cfd2d" 2>/dev/null; pkill -9 -f "run_all" 2>/dev/null; sleep 1; echo "done" |

## Reviews

- Code review scorecard: `codex_glm52_02_06cf18/review_code.md` (overall: 4.2)
- CFD methods review: `codex_glm52_02_06cf18/review_cfd.md` (overall: 3.96)
- Result review: `codex_glm52_02_06cf18/review_results.md` (overall: 4.25)

# AGENTS.md

Project instructions for coding agents working in this repository
(manager branch).

## Mission

This repository is the benchmark **manager** workspace, not a contestant
workspace. It exists to set up and operate the CFD solver agentic benchmark.
The work here is:

1. **Create initial workspaces for benchmark contestants** — one fresh
   workspace per contestant run, as siblings of this repo (e.g.
   `codex_glm52_m3_01`): checkout this template on the contestant's
   `codex/*/init` branch, symlink `external/` to the shared DNDSR-style
   externals, pin the benchmark submodule, and leave the contestant to build
   `solver/` inside the workspace.
2. **Manage the benchmark repo** — maintain the
   `cfd_solver_agentic_benchmark` submodule (task spec, input cases/meshes,
   examiner validator and rubric) and pin it to reviewed commits. For
   contestants that submodule is read-only input.
3. **Build evaluation utilities** — run `examiner/validate_outputs.py` and
   the scoring rubric against contestant results, collect graded artifacts
   (e.g. under `cfd_solver_agentic_benchmark_results/`), and add
   manager-side tooling for validation and scoring.
4. **Agent-execution tooling (secondary)** — tools to run and observe the
   agents themselves: the `opencodex` submodule (ocx provider proxy) and
   `ocx-relay` (wire capture), plus related research notes such as
   `OPENCODEX_429_RETRY.md`.

## Repository layout

- `cfd_solver_agentic_benchmark/` — benchmark input repo (submodule):
  `TASK.md`, input cases/meshes, `examiner/` validator + rubric. Read-only
  for contestants; manager-maintained.
- `opencodex/` — ocx provider proxy (submodule); agent-execution tooling.
- `ocx-relay/` — wire-capture relay (submodule); verifies reasoning-context
  preservation through ocx.
- `external/` — symlink to the shared DNDSR-style externals
  (`cfd_externals/install`, Eigen, fmt, ...). Reference only; do not copy
  into contestant workspaces.
- `.venv/` — local Python environment for benchmark tooling.

## Services

- Never stop, start, or restart the `ocx` service. The ocx proxy lifecycle is
  user-owned; agents may use its management API/CLI for read-only inspection
  and model/catalog operations, but must not touch service state.

## Git Discipline

- Do NOT run `git pull`, `git push`, or switch branches without explicit user
  authorization. Unauthorized remote or branch mutations are prohibited.
- Do use `git commit` (and `git stash` when needed) freely as code checkpoints:
  commit or stash working progress regularly so partial work can be resumed or
  rolled back. Local checkpoint commits do not require approval; only pushing
  and branch/remote mutations require it.
- Commit submodule moves explicitly (e.g. `git add cfd_solver_agentic_benchmark`)
  and pin benchmark submodule changes to reviewed commits.

## Subagents

- Delegating independent subtasks to subagents is encouraged, especially for
  the four work areas above.
- Manager work is not model-pinned: use the session's available subagent
  models. When preparing or working inside a contestant workspace, honor that
  workspace's `AGENTS.md` subagent policy.

## Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

# AGENTS.md

Project instructions for coding agents working in this repository.

## 1. Git Discipline

- Do NOT run `git pull`, `git push`, or switch branches without explicit user
  authorization. Unauthorized remote or branch mutations are prohibited.
- Do use `git commit` (and `git stash` when needed) freely as code checkpoints:
  commit or stash working progress regularly so partial work can be resumed or
  rolled back. Local checkpoint commits do not require approval; only pushing
  and branch/remote mutations require it.

## 2. Subagents

- Delegating independent subtasks to subagents is encouraged.
- Subagent model policy: the allowed subagent models are `BLSC/GLM-5.2` and
  `BLSC/MiniMax-M3` (passed as the `model` override to `spawn_agent`).
  Do not spawn subagents with any other model.
- Visual handoff (important): whenever a subagent produces something visual —
  a report, plot, figure, or postprocessing output — hand it off to a
  `BLSC/MiniMax-M3` subagent for visual checks and design review before the
  task is considered done.

## 3. Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

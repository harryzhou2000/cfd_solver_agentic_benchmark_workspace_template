# AGENTS.md

Project instructions for coding agents working in this repository.

## Sandbox (read/write restriction)

- The agent is FORBIDDEN from reading or writing any directory other than the
  current working directory (cwd). This includes `/tmp` and the parent
  directory. All file reads and writes must stay inside the workspace.

## 1. Git Discipline

- Do NOT run `git pull`, `git push`, or switch branches without explicit user
  authorization. Unauthorized remote or branch mutations are prohibited.
- Do use `git commit` (and `git stash` when needed) freely as code checkpoints:
  commit or stash working progress regularly so partial work can be resumed or
  rolled back. Local checkpoint commits do not require approval; only pushing
  and branch/remote mutations require it.

## 2. Subagents

- Delegating independent subtasks to subagents is encouraged.
- Every subagent spawned from this repository MUST use the model
  `BLSC/GLM-5.2` (passed as the `model` override to `spawn_agent`).

## 3. Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

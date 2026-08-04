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
- Dispatch subagents with the `task` tool (normal dispatch) or the same
  `task` tool with `background: true` (experimental background subagents;
  requires `OPENCODE_EXPERIMENTAL_BACKGROUND_SUBAGENTS=true`); either form
  may be used. `subagent_type` selects the agent, `prompt`/`description`
  carry the task, and `task_id` resumes an earlier subagent session.
- There is NO subagent model policy on this branch: subagents may use any
  available model. No model is required, restricted, or pinned.
- Visual handoff (important): the orchestrator and other subagents are NOT
  visual-enabled. Whenever a subagent produces something visual — figures,
  plots, reports, or rendered output — hand it off to a `designer` subagent
  to check and fix visual problems before the task is considered done.

## 3. Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

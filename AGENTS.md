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
- Subagent model policy: use self-model delegation only. Every subagent must
  inherit the main Claude Code session's model. Omit the Agent tool's `model`
  parameter. Any explicit subagent model override is strictly forbidden.

## 3. Use Subagents (Important)

- Delegating independent subtasks to subagents is encouraged. Delegate
  the following task types instead of doing the work in the main agent:
  code audit, deep research, review, and mechanical execution.
- Code audit: spawn a subagent to audit code for bugs, contract
  compliance, hidden shortcuts, and originality; require file:line
  evidence in its report.
- Deep research: spawn a subagent to investigate APIs, dependencies,
  numerical methods, or benchmark requirements before implementing.
- Review: spawn a subagent to critique plans, code, reports, and results
  before they are finalized.
- Mechanical execution: spawn subagents for repetitive work (build
  variants, case sweeps, file conversions, formatting, cleanup) so it
  runs in parallel instead of blocking you.
- Use Claude Code's Agent tool with a short `description`, a self-contained
  `prompt`, and an appropriate `subagent_type` such as `general-purpose` or
  `Explore`. Omit `model` so delegation inherits the current model:

  ```text
  Agent(
    description: "Audit solver implementation",
    prompt: "Audit <scope> and report concrete findings with file:line evidence.",
    subagent_type: "general-purpose",
  )
  ```

- Launch independent Agent calls concurrently when useful. Use
  `run_in_background: true` only for work that can proceed asynchronously,
  and resume an existing agent when iterating instead of starting a duplicate.
- Subagents share the workspace but receive only the task context supplied in
  their prompt. Give each one a bounded scope, relevant paths and constraints,
  a concrete deliverable, and a request for a concise written report.

## 4. Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

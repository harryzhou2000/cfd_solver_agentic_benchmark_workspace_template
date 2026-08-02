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
- Subagent model policy: the ONLY allowed subagent model is
  `BLSC/DeepSeek-V4-Flash` (passed as the `model` override to `spawn_agent`).
  Do not spawn subagents with any other model.

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
- Use the spawn tool with a concrete task name and a self-contained
  message (passed as the `task_name` and `message` overrides to
  `spawn_agent`). When the subagent model differs from yours, also pass
  the `model` override with `fork_turns: "none"`; a full-history fork
  inherits your model and rejects model overrides:

  spawn_agent(
    task_name: "code_audit",
    message: "Audit <scope> and report findings with file:line evidence.",
    model: "<subagent model>",
    fork_turns: "none",
  )

- Subagents share the same workspace and can read and write files, but
  `fork_turns: "none"` means they do not inherit the conversation
  history, so the message must be self-contained. Give each one a clear
  deliverable and ask for a concise written report; use `followup_task`
  for iteration and `wait_agent` to collect results.

## 4. Persistence

- Once the user has started a task, do not stop until the specified task is
  accomplished to a level of satisfaction: completed results, validated
  artifacts, and an honest report. Work through blockers with safe in-scope
  checks and alternatives; only stop early when genuinely blocked or explicitly
  told to stop.

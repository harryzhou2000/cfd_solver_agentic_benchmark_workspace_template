# Goal-plugin compaction continuation hardening plan

Date: 2026-08-20

## Problem and evidence

The recorded `workspace/oc-goal/kimik3-dsv4/05` run entered a runaway OpenCode
auto-compaction loop near completion. OpenCode 1.18.11 created 947 automatic
compactions, and the retained-tail boundary did not advance. The goal was no
longer active during the later loop, so the root failure is OpenCode's scheduler,
not a goal-plugin continuation by itself. OpenCode 1.18.18 and
`opencode-goal-plugin@0.8.1`, which are currently pinned by the Docker harness,
do not contain a complete no-progress termination guard.

For an active goal, however, the plugin currently clears its continuation claim
on every `session.compacted` event. That preserves the desired post-compaction
resume when OpenCode retains the same assistant message, but it gives repeated or
stalled compactions no epoch identity or bounded failure path.

## Required behavior

For every successful compaction while a goal is active:

1. Disable OpenCode's generic post-compaction continuation through
   `experimental.compaction.autocontinue`.
2. Accept `session.compacted` as the start of a new context epoch.
3. Wait for the session to become idle.
4. Send exactly one goal-aware continuation for that compaction epoch.

The same retained assistant message ID must be eligible again in a new epoch.
Repeated idle events within one epoch must not send more than one continuation.

## Plugin design

- Add a monotonically increasing `compactionEpoch` to each goal and include it
  in the durable continuation claim alongside `runId` and
  `sourceAssistantMessageID`.
- Coalesce repeated `session.compacted` deliveries when OpenCode supplies a
  stable event, compaction, or summary identity.
- On a new successful compaction, increment the epoch, reset context-epoch token
  tracking, and invalidate any pre-compaction idle handler. Do not preserve the
  old claim as current: doing so would recreate the post-compaction stall fixed
  by commit `aa1f366`.
- Treat assistant messages marked `summary: true` or with agent/mode
  `compaction` as compaction machinery, not productive goal work or a source for
  completion/progress decisions.
- Track consecutive compactions since the last productive non-compaction
  assistant/tool turn. On the second compaction without such progress, pause the
  goal and best-effort abort the OpenCode session to break the active-goal loop.
- Keep lifetime usage accounting separate from the context-epoch high-water
  mark. This PR should not silently redefine the existing `maxTokens` context
  budget; a separate change can add a lifetime-token limit if desired.

## Regression coverage

Add behavior tests proving:

- one successful compaction followed by idle sends exactly one continuation;
- a retained assistant ID can be continued once in each new epoch;
- duplicate delivery of the same identifiable compaction does not continue
  twice;
- repeated compaction/idle cycles without productive work trip the circuit
  breaker and invoke best-effort abort;
- a productive non-compaction assistant/tool turn resets the stalled counter;
- compaction-summary assistants do not reset it or become continuation sources;
- a compaction arriving after claim persistence but before `promptAsync`
  invalidates the stale idle handler;
- native compaction autocontinue remains disabled only for active, running goals.

Run the focused Node test, then every check listed by `CONTRIBUTING.md` before
submitting the pull request.

## Ownership and rollout

This plugin mitigation is deliberately scoped to sessions with an active goal.
It cannot guarantee termination after the goal is archived or when no goal was
ever active; OpenCode still needs a scheduler-level retained-tail/no-progress
guard. Upstream OpenCode PR #28139 is a useful design reference but was closed
unmerged.

After the goal-plugin PR is reviewed and released, update both Docker plugin
references from `npm:opencode-goal-plugin@0.8.1` to the reviewed release in one
manager change, rebuild the image, and validate a real compaction/idle cycle.
Do not pin the Docker harness to an unpublished feature branch.

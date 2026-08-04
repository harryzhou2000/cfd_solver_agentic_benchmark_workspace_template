# Goal-plugin fork, no-interrupt patch, and PR

Context: goal-plugin pause-on-user-message behavior caused false "user
intervention" pauses (e.g. omo_slim_dsv4_06 paused 55s in by synthetic
background-task result messages), and interrupted otherwise healthy goals.

## Fork / patch / PR

- Upstream: https://github.com/willytop8/OpenCode-goal-plugin (v0.7.0, main).
- Fork: https://github.com/harryzhou2000/OpenCode-goal-plugin
  (forked 2026-08-05; `main` stays clean at upstream v0.7.0).
- Patch branch: `feat/no-interrupt-user-message` (commits `03c493c` +
  `223cc87`).
- PR: https://github.com/willytop8/OpenCode-goal-plugin/pull/53

## The patch (minimal)

Two new plugin options (both default `false`; backward compatible):

1. `noInterruptOnUserMessage` — when `true`, a new human message does not
pause an active goal with `stopReason: "user intervention"` — the goal loop
keeps running and the message steers the next continuation (Codex-style
steering). Gates the three pause sites in `src/goal-plugin.js`:

1. `chat.message` hook (~line 4427): early return instead of
   `pauseActiveGoal(..., { abortAccepted: true })`.
2. Auto-continue claim guard (`claimContinuationSource`, ~line 4251):
   `newHumanMessage || userInterventionDetected(...)` pause is gated.
3. Idle continuation driver (~line 5237): `userInterventionDetected(...)`
   pause is gated.

2. `noContinueWhileChildrenActive` — when `true`, auto-continue is deferred
while the session has active child sessions (subagents/background tasks): the
goal stays running but the goal loop does not prompt the orchestrator over
work a child is already doing. The gate lives in `claimContinuationSource`
(covers both continuation paths: normal continue and budget wrapup) and
checks opencode's `children` + `status` endpoints through the shape adapter
(`children`/`status` added as replay-safe read-only operations in
`src/opencode-session-api.js`). Hosts that cannot report children/status fail
open (continuation proceeds).

Options flow through `DEFAULT_OPTIONS` + `normalizeOptions` into
`defaultGoalOptions`, so plugin config reaches `goal.options` on new and
persisted goals. Docs updated in README.md, index.d.ts, CHANGELOG.md; tests
added in test/goal-plugin.test.js and test/opencode-session-api.test.js
(356/356 pass on official Node 18/20/22/24).

## Local checkout

Submodule in the manager workspace: `OpenCode-goal-plugin/` pointing at the
fork, pinned at `223cc87` (parent commit updated alongside). Remote `origin` is SSH
(`git@github.com:harryzhou2000/OpenCode-goal-plugin.git`); `.gitmodules` uses
the HTTPS URL per repo convention.

## Checks

- `npm run check` — 356/356 pass on official Node 18/20/22/24 (final commit).
- `npm run smoke` and `npm run type:check` — pass.
- Pre-existing, unrelated failures on pristine main in this environment:
  `test/persistence-lease.test.js` hangs; `test/session-concurrency.test.js`
  fails. Reproduced identically with the patch stashed.

## Live use

The installed plugin under `~/.cache/opencode/packages/...` is NOT patched
(npm registry copy is overwritten on update). To use the behavior now, point
the opencode plugin config at the fork/branch (git source) or wait for the PR
to merge; then set `"noInterruptOnUserMessage": true` in the plugin tuple,
e.g.:

```jsonc
[
  "opencode-goal-plugin",
  {
    "noInterruptOnUserMessage": true,
    "noContinueWhileChildrenActive": true
  }
]
```

omo-slim's own auto-continuation (`backgroundJobs.continueOnIdle`) was tried
and reverted to `false`; the goal plugin's children-aware deferral covers the
background-wait case without a second continuation system.

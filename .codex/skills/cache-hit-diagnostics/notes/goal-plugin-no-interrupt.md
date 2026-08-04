# Goal-plugin fork, no-interrupt patch, and PR

Context: goal-plugin pause-on-user-message behavior caused false "user
intervention" pauses (e.g. omo_slim_dsv4_06 paused 55s in by synthetic
background-task result messages), and interrupted otherwise healthy goals.

## Fork / patch / PR

- Upstream: https://github.com/willytop8/OpenCode-goal-plugin (v0.7.0, main).
- Fork: https://github.com/harryzhou2000/OpenCode-goal-plugin
  (forked 2026-08-05; `main` stays clean at upstream v0.7.0).
- Patch branch: `feat/no-interrupt-user-message` (commit `03c493c`).
- PR: https://github.com/willytop8/OpenCode-goal-plugin/pull/53

## The patch (minimal)

New plugin option `noInterruptOnUserMessage` (default `false`; backward
compatible). When `true`, a new human message does not pause an active goal
with `stopReason: "user intervention"` — the goal loop keeps running and the
message steers the next continuation (Codex-style steering). Gates the three
pause sites in `src/goal-plugin.js`:

1. `chat.message` hook (~line 4427): early return instead of
   `pauseActiveGoal(..., { abortAccepted: true })`.
2. Auto-continue claim guard (`claimContinuationSource`, ~line 4251):
   `newHumanMessage || userInterventionDetected(...)` pause is gated.
3. Idle continuation driver (~line 5237): `userInterventionDetected(...)`
   pause is gated.

Option flows through `DEFAULT_OPTIONS` + `normalizeOptions` into
`defaultGoalOptions`, so plugin config reaches `goal.options` on new and
persisted goals. Docs updated in README.md, index.d.ts, CHANGELOG.md; tests
added in test/goal-plugin.test.js (276/276 pass).

## Local checkout

Submodule in the manager workspace: `OpenCode-goal-plugin/` pointing at the
fork, pinned at `03c493c` (parent commit `587a75c`). Remote `origin` is SSH
(`git@github.com:harryzhou2000/OpenCode-goal-plugin.git`); `.gitmodules` uses
the HTTPS URL per repo convention.

## Checks

- `node test/goal-plugin.test.js` — 276/276 pass.
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
["opencode-goal-plugin", { "noInterruptOnUserMessage": true }]
```

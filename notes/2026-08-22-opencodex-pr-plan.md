# OpenCodex PR Plan — empty-output annotation, config-mutation audit, divergence warning

Date: 2026-08-22
Base: opencodex upstream/dev @ a69d291fb (submodule on local dev)

## Context

Session 01a02735-5ca9-7711-99e1-0a3defaf3ba4 showed duplicate tool use: the agent
re-issued identical commands because the exec tool returned present-but-empty outputs
(the model's scripts did not call `text(...)`). ocx only annotates MISSING tool results
(`[ocx] no tool result was recorded ...`), not present-but-empty ones, on the
OpenAI-compatible adapters. Anthropic/Gemini/Cursor already normalize empty outputs.

Separately, the vLLM overflow notes (2026-08-22) identified two real ocx enhancements:
management config mutations are un-auditable, and resident/disk config divergence is
silent (config loaded once at startup; no reload API).

## PR A — Optional empty tool-output annotation (DeepSeek default ON)

Behavior:
- Add provider option `annotateEmptyToolOutputs?: boolean` (name TBD in review).
- When enabled, a tool result that is present but empty (no usable text/content) is
  rewritten to an explicit annotation, e.g. `(empty tool output)` or the Cursor-style
  actionable hint. Non-empty results stay byte-identical.
- Missing-result placeholder behavior stays unchanged.
- Default ON in the DeepSeek registry seed; other vendors unchanged.

Anchors:
- `src/adapters/openai-chat.ts` — tool-result message construction / `flushPendingToolCalls`
- `src/adapters/openai-responses.ts` — tool-output repair/synthesis path
- `src/types.ts` — provider config field
- `src/providers/registry.ts` — deepseek seed (DEEPSEEK_THINKING_MODELS region)
- Tests: chat + responses unit tests for empty/non-empty/missing; deepseek default test

Acceptance:
- Empty output on deepseek (chat and responses wires) becomes annotated.
- Non-empty and missing-result behavior unchanged.
- Typecheck + affected suites green.

## PR B — Config mutation audit log

Behavior:
- Record every config mutation: route/CLI path, actor, changed fields, before/after,
  timestamp.
- Expose via management API (e.g. `GET /api/config/mutations` or an /api/logs section).
- Bounded ring buffer; no secrets in before/after (redact like existing logging).
- Persist option: keep history across restarts if cheap (file/sqlite upgrade), else
  in-memory with a clear limitation.

Anchors:
- `src/server/management/config-routes.ts`, `provider-routes.ts`, `system-routes.ts`,
  settings/shadow-call/sidecar routes — all mutation paths
- `src/config.ts` — central save wrapper so every write is captured
- CLI writes (`src/cli/provider.ts`, `src/cli/config*`) go through the same save wrapper
- Tests: each mutation route records actor/fields/before/after; redaction verified

Acceptance:
- Dashboard/CLI mutation leaves an audit row with before/after; secrets redacted.

## PR C — Config divergence warning (ocx status + GUI card)

Behavior:
- Server exposes resident-vs-disk divergence (e.g. `GET /api/config/status` →
  `{ residentVersion, diskVersion, diverged }`).
- `ocx status` prints a warning line when diverged (CLI output, not just JSON).
- GUI shows a warning card in the same style as the existing
  "Codex does not depend on the local proxy" card
  (`gui/src/pages/dashboard-overview-head.tsx` uses i18n `startup.summary.native`,
  defined in `gui/src/i18n/en.ts`). Card offers "Restart to apply" (POST /api/system/restart).
- UI audit: run the GUI locally, capture a screenshot of the card, and verify
  placement/readability with a **gpt-5.6-terra subagent (vision)** before pushing.

Anchors:
- `src/server/management/config-routes.ts` or `system-routes.ts` — status endpoint
- `src/cli/status.ts` — printed warning + CliStatusJson field
- `gui/src/pages/dashboard-overview-head.tsx`, `gui/src/i18n/en.ts` — card + strings
- Tests: status endpoint unit test; CLI output test; i18n key test

## Workflow

For each PR (branches from upstream/dev):
1. Local fix + tests + typecheck
2. Self-audit (targeted suites; known /tmp-identity failures compared to clean dev)
3. Push to fork, draft PR, tick readiness checklist
4. Wait for CodeRabbit/gate; fix/reply/resolve until green
5. Undraft only when checklist complete; ping maintainer once when needed
6. Keep PRs rebased on current dev

Known env note: ~51-59 catalog tests fail locally on /tmp identity ownership; compare
against clean upstream/dev before judging regressions.

## Status 2026-08-25

All three PRs rebased onto upstream/dev @ 98ed186c7 (current dev) and pushed:

- A — PR #2350 (fix/annotate-empty-tool-outputs): moved the
  annotateEmptyToolOutputs boolean validation off src/server/auth-cors.ts
  (zero diff vs dev) into src/config/provider-validation.ts +
  src/server/management/provider-routes.ts, so hygiene passes without
  maintainer sponsorship. Local: 106 annotation/management + 23 dangling-repair
  pass, typecheck clean, diff --check clean. Undrafted; CodeRabbit completed.
- B — PR #2351 (feat/config-mutation-audit): rebased, trailing-blank-line fix,
  local audit 30 + config 154 + lock/auth 33 + save/user-edits 41 pass,
  typecheck clean. Undrafted; CodeRabbit completed with no actionable comments.
- C — PR #2355 (feat/config-divergence-warning): rebased; local divergence 16 +
  CLI 12 + plan 11 + GUI contracts 16 pass, typecheck clean. Terra (gpt-5.6)
  vision audit of the full-dashboard screenshot: READY (warning bar matches the
  startup-health bar style, amber dot, exact i18n copy, no clipping). Still
  draft: the WHAM adoption re-anchor necessarily touches src/codex/auth-api.ts
  (unsponsored_surface); one maintainer ping posted (2026-08-25) for
  maintainer-sponsored.

Remaining: wait for bot/maintainer convergence on all three; re-undraft A/B if
the gate bot re-drafts after a body/push event; land C once sponsored.

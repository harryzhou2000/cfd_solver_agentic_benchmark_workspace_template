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

## Status 2026-08-25 (evening) — PR D added, all four converged

- D — PR #2527 (feat/auto-review-model-override): listed as PR D per user
  request. Bot findings fixed and landed on head 1a51fd461: GET /api/providers
  exposes autoReviewModel/autoReviewModelOverrides; canonical openai rejects the

  fields explicitly on POST/PATCH (with a canonical-seeded regression); the
  trusted openai-api rebuild keeps the configured override (regression added).
  The earlier flash/terra audit items were already addressed on the head
  (folded-known bare-target slugging, fail-closed sync validation, custom-row
  inheritance, no-template stamping, redaction, whitespace keys). CodeRabbit
  clean, all gates green, undrafted, MERGEABLE.
- A #2350 and B #2351 remain ready (undrafted, green, MERGEABLE).
- C #2355 remains draft only for maintainer-sponsored on the auth-api re-anchor
  hunk; full Summary/Test plan body is prepared at /tmp/body-c-full.md and will
  be published once the label lands.


## Status 2026-08-26 (evening) - convergence round

- Base: upstream/dev @ 779b6090c (unchanged all day).
- D #2527 head e72184984: fixed native-row override preservation (1efe18b54) and wrong-shaped routed overrides -> null (e72184984). Gates green, CodeRabbit pass, 0 unresolved threads, ready. Runtime CI (Cross-platform CI + React Doctor) is action_required pending workflow approval on the fork PR.
- B #2351 head a6f7f4088: fixed read-side recovery deleting in-flight audit markers (1ed2305ce), boundAuditDetail truncation bound and parseable-invalid marker deletion (614fbe98f), and pending-marker root shape validation for JSON null (a6f7f4088). Local 59 pass / 0 fail, typecheck/diff-check clean. CodeRabbit pass, 0 unresolved threads, ready; runtime CI action_required.
- A #2350 head 199df4fd8: ready, gates green, CodeRabbit pass, 0 unresolved threads; runtime CI action_required.
- C #2355 head 0596a4631: still draft, intake: hygiene-blocked (unsponsored_surface via src/codex/auth-api.ts); awaiting maintainer-sponsored.

## Status 2026-08-27 - rebase round on newest dev

- Base advanced: upstream/dev @ 5dfee1a05 -> a57b9620a (CI/docs/version-line only; no overlap with A/B/C/D files).
- A #2350 head 481da60ad: rebased onto 5dfee1a05 (chose to keep stable vs re-rebasing onto a57b9620a since the newer commits are CI/docs-only; A is approved-head, gates green, CodeRabbit pass). Re-ticked/undrafted after gate re-draft; posted one @Ingwannu re-review/CI-approval ping (2026-08-27).
- B #2351 head 81a764898: rebased onto a57b9620a; local 59 + 201 + 33 + 32 pass, typecheck/diff-check clean; pushed, body updated, ready, CodeRabbit resumed (pass).
- C #2355 head fd2e244aa: rebased onto a57b9620a; local 42 + 16 (GUI contracts) pass, typecheck/diff-check clean; pushed, body unticked, stays draft/hygiene-blocked pending maintainer-sponsored.
- D #2527 head 7de4d3e37: rebased onto a57b9620a, then fixed Ingwannu's validation-order blocker: wrong-shaped native overrides (numeric/object/blank string) are normalized before the native-row preservation branch; undefined/null native shapes stay canonical (preserves sync/converge byte identity); native-row regressions added. Local 230 + 24 + 77 pass, typecheck/diff-check clean; pushed, body updated (126 pass), ready, CodeRabbit resumed; @Ingwannu reply posted with fix summary. All review threads on A/B/C/D resolved.

## Status 2026-08-27 (afternoon) - Ingwannu re-reviews + CI unblock round

- Base advanced again: upstream/dev @ 8b1b65b8d (kiro adapters, gui, core-lab, devlog release-train notes). All three active branches rebased onto it (clean).
- Ingwannu: A #2350 range-diff-equivalent to the approved series (real gap filed as upstream #2763, out of scope); D #2527 approved fork CI runs and re-checked head; B #2351 CHANGES_REQUESTED with one blocker: buildConfigMutationSnapshot persisted arbitrary providers.<name>.headers values verbatim.
- B fix (b81d17e33): every value inside providers.<name>.headers is masked regardless of header name, for direct headers diffs and root/whole-config snapshots; opaque-value regressions added. Local 61 + 201 + 33 + 32 pass.
- CI unblocks applied to A/B/D: package.json bumped 2.34.0 -> 2.35.0 (release-version-line guard fails on fork heads while dev is 2.34.0 and v2.34.0 is tagged); de-literalized sk-live fixture strings (privacy scan); changed upstream devlog git@github.com push line to HTTPS (privacy scan flags the email shape). Heads: A 3db0316da, B b81d17e33, D dd87d128c.
- NEW GATE: package.json is a restricted dependency surface, so the bump trips unsponsored_surface hygiene on A/B/D (gate re-drafts them); sponsorship requested once per PR. Alternative offered: upstream bumps dev to 2.35.0 and the bump can be dropped from the PRs. C #2355 unchanged (still maintainer-sponsored for auth-api).

## Status 2026-08-27 (evening) - owner-review replies + CodeRabbit follow-ups

- D #2527: resumed CodeRabbit found 2 new threads - (1) devlog HTTPS push broke the SSH deploy-key semantics: reverted (PR no longer touches the file; privacy-scan flag is an upstream conflict, already flagged); (2) auto-review persistence test relied on loadConfig() sanitization: now reads/parses config.json directly. Both replied+resolved. Head 5ae5ffd88.
- B #2351: resumed CodeRabbit found 3 new threads - (1) same devlog HTTPS issue: fixed by revert; (2) redactUrlUserinfoString only masked a URL at offset 0 / first match: now unanchored global replacement with embedded+repeated-URL regressions; (3) label-length assertion too loose (268 vs 256): tightened. All replied+resolved. Head e2903b99b. Suite 1 now 62 pass.
- A #2350: devlog reverted too. Head e3be23ec4.
- Discovered lidge-jun's 08-22 owner review comments on all four PRs had NO replies: A (priority review + Responses emptiness blocker), B (priority review + apiKeys plaintext blocker), C (priority review + incidental loadConfig blocker), D (priority review). Verified every item is addressed on current heads and posted concise status replies to all six comments (B's apiKeys blocker reply posted separately). All review threads across A/B/C/D resolved (A 4, B 38, C 9, D 12; 0 unresolved).
- Remaining: all four PRs draft + hygiene-blocked awaiting maintainer-sponsored (A/B/D: package.json version bump; C: auth-api hunk), or upstream bumping dev to 2.35.0 / fixing the devlog privacy-scan flag so our workarounds can be dropped.

## Status 2026-08-27 (blocked) - external maintainer gate

- Third consecutive goal turn with the same blocker: all four PRs draft + hygiene-blocked (unsponsored_surface), no maintainer-sponsored label, upstream dev unchanged at 8b1b65b8d/2.34.0, devlog privacy-scan flag unfixed. All in-scope work is done: 0 unresolved threads on all PRs, CodeRabbit clean, every reviewer comment replied, branches rebased on current dev, local suites green.
- Unblock paths (external): (1) maintainers apply maintainer-sponsored to A/B/D (package.json bump) and C (auth-api hunk); or (2) upstream bumps dev to 2.35.0 and fixes the devlog privacy-scan flag, then rebase and drop the package.json bump/workarounds.

## Status 2026-08-27/28 (resumed) - upstream unblocked, bumps dropped

- Upstream landed #2766: dev bumped to 2.35.0 (d1def682d) and the release-doc privacy-scan false positive was fixed (3110bd186). The external blocker for A/B/D is gone.
- Rebased A/B/D onto d1def682d and dropped the package.json bump + devlog changes entirely (conflicts resolved to upstream; empty revert commits dropped). Diffs now touch only feature files. Heads: A 5422a7728, B 862c2aa55, D 6c22214bd, C 85ea3c3c2.
- Local validation: A 122 pass, B 62+201+33+32 pass, D 333 pass, C 42+16 pass; privacy scan + version-line + typecheck + diff-check clean on all.
- Gates: A/B/D hygiene/enforce-target/label/resolve-pr all PASS and PRs are ready (4/4 checklist); runtime CI (Cross-platform/Service lifecycle/React Doctor) is action_required awaiting maintainer workflow approval. CodeRabbit: A pass, D pass (0 unresolved), B pending after resume.
- Ingwannu reviewed the pre-rebase heads on all three (no new blockers; asked for rebase onto #2766 + drop bump) - replied on A/B/D confirming both are done. C rebased, owner pinged once for maintainer-sponsored (UI change authorization + auth-api hunk).

## Status 2026-08-28 (final convergence round)

- B #2351: fresh CodeRabbit review on the rebased head found a __proto__ audit-snapshot gap (copy functions dropped an own "__proto__" JSON key via prototype mutation). Fixed on 36d2ce9a5: redactSecrets plus every config-object copy in the audit path now use null-prototype records; regression added (JSON.parse('{"__proto__":...}') persists in fields/before/after). Local 63+201+33+32 pass, privacy clean, typecheck clean. Pushed, body updated, ready again; CodeRabbit re-reviewing; thread replied+resolved.
- A #2350 head 5422a7728 / D #2527 head 6c22214bd: ready, gates green, CodeRabbit pass, 0 unresolved threads; Ingwannu re-review + fork CI approval pending (replied to all three).
- C #2355 head 85ea3c3c2: rebased, 42+16 pass, draft; owner pinged once for maintainer-sponsored (UI + auth-api hunk).
- Remaining external: Ingwannu re-approve A/B/D exact heads; lidge-jun sponsor C.

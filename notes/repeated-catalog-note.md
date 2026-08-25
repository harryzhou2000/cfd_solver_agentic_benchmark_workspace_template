# Repeated catalog-change note (live reproduction)

Exact `<multi_agent_mode>` line injected repeatedly in session
`019fb87c-a029-79a3-bb4d-033c32e9d84d`:

> The model catalog changed after Codex started; do not set model or
> reasoning_effort overrides until Codex restarts.

## Corrected diagnosis

This is OpenCodex guidance appended once per stateless upstream request. It is
not primarily an accumulation of duplicate items in Codex's saved history.
The exact sentence is absent from Codex 0.146.1 source and is authored by
OpenCodex's `multiAgentGuidanceText()`.

The affected Codex 0.146.0 rollout sends no `previous_response_id`, and the
proxy-generated developer item is absent from its locally stored response
items. OpenCodex therefore appends a fresh item to every request. Because that
item is placed after the current user message or tool result, the routed model
often treats it as the newest event and acknowledges it instead of silently
continuing the task.

This session used HTTP Responses traffic. Codex's HTTP request type has no
`previous_response_id`; that continuation field exists on its WebSocket
request type. OpenCodex currently has WebSockets disabled, so Codex falls back
to the stateless HTTP path with `store: false` and the complete local prompt.

Measured from `2026-08-09T18:29:38Z` through the inspected tail:

- 192 assistant messages were stored.
- 151 mentioned the catalog or repeated injected note.
- The exact warning appeared in 28 stored reasoning records.
- 283 tool calls occurred while this was happening.
- One compaction at `19:33:09Z` did not stop the behavior.

The first affected task asked the agent to address a PR maintainer comment.
The agent acknowledged the note at `18:30:29Z`, then repeatedly narrated it
through implementation, testing, commit, push, rebase, and PR polling. At
`18:38:13Z` the user explicitly added investigation of the repetition as a
side task. At `19:42:24Z` the user asked for this reproduction note to be
written, while the same note continued to be appended to subsequent requests.

## Trigger

OpenCodex emits this v2 warning when collaboration tools are present and its
host-global catalog check reports any matching Codex app-server as stale or
unknown. A stale unrelated app-server can therefore trigger the warning for a
newly resumed CLI request.

## Mitigation

To stop the behavior immediately, disable all OpenCodex collaboration guidance
with `ocx agent injection set --guidance off`. This also disables useful v1
proactive and v2 roster guidance.

Synchronizing the catalog and restarting all matched Codex app-servers with
`ocx sync --restart-codex` removes the stale warning, but fresh-catalog roster
guidance may still be injected on every HTTP request. Enabling OpenCodex
WebSocket support may allow Codex's stateful continuation path and existing
replay-prefix dedup, but should be treated as a separately tested transport
change rather than an immediate production workaround.

The durable code fix is to keep necessary per-request guidance in a stable
system/developer prefix before conversational input, or suppress the stale
warning and expose it through diagnostics instead. Full-input dedup alone
cannot fix this Codex path because the previous proxy-only item is not in the
next Codex request.

See `opencodex-guidance-reinjection.md` for source paths and the broader audit.

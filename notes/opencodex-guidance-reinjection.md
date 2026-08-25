# opencodex multi-agent guidance reinjection — root-cause report

> **Correction after tracing session `019fb87c-a029-79a3-bb4d-033c32e9d84d`:**
> the affected Codex legacy HTTP client does not persist OpenCodex's
> proxy-only developer item in its local rollout. OpenCodex injects once per
> stateless upstream request, and the main behavioral defect is that the item
> is appended after the current user/tool input. Full-input dedup therefore
> does not fix this client. See `repeated-catalog-note.md` for the measured
> tail trajectory. The replay-prefix analysis below remains applicable to
> clients that use `previous_response_id`, but its client-history accumulation
> claims and full-input-dedup recommendation do not describe this Codex path.
> Codex HTTP Responses requests have no `previous_response_id`; Codex only
> exposes that continuation field on its WebSocket request type. With
> OpenCodex WebSockets disabled, the affected session fell back to HTTP with
> `store: false`, so transport configuration is part of the trigger.

Audit date: 2026-08-10 (Asia/Shanghai) · source commit `c56ceb25a024088ea7083190e30545f2ded2867b` on `feat/provider-cost-overlay` · live service PID 1412890 on `localhost:10109` (read-only inspection only; not restarted).

## Summary

opencodex re-injects the same `<multi_agent_mode>` developer guidance into every turn for stateless HTTP Responses clients. The dedup path is tied to `previous_response_id` replay (`_replayPrefixLen`); stateless clients never send `previous_response_id`, so `_replayPrefixLen` is 0, the dedup scan examines `raw.input.slice(0, 0)` (an empty array), and `injectDeveloperMessage()` appends a fresh copy to the request every time. The client then keeps and resends its growing conversation, so copies accumulate both in the parent conversation and in the proxy's response-state cache/spill files.

The earlier hypothesis is confirmed: the response-state cache does not prevent or even participate in dedup for stateless requests. It is keyed by response ID, only consulted when a request carries `previous_response_id`, and is not configurable via config/env.

## Exact trigger conditions

All of the following must hold for the injection to run:

- The request is Responses-shaped (`POST /v1/responses`), parsed by `parseRequest()` and handled by `handleResponses()` in `opencodex/src/server/responses/core.ts` (`core.ts:1360-1415`, injection call at `core.ts:967-984`).
- The request has an `input` array (the parser also handles `input` strings, but injection into `raw.input` only happens for arrays).
- The request declares a collaboration surface via tools:
  - `collabSurface()` in `opencodex/src/server/responses/collaboration.ts:145-166` requires at least one `spawn_agent` tool.
  - v1 companions: `send_input`, `resume_agent`, `close_agent`; namespaced `spawn_agent` without companions defaults to v1.
  - v2 companions: `send_message`, `followup_task`, `interrupt_agent`, `list_agents`; flat `spawn_agent` without companions defaults to v2.
  - Contradictory shapes (`namespaced + flat` spawn, or both v1 and v2 companions) return `null` and suppress injection.
  - Tools may arrive via `body.tools` or `additional_tools` inside `input`; the parser merges both before surface detection (`parser.ts:344-353`, `parser.ts:618-631`).
- `multiAgentGuidanceText()` returns text:
  - Returns `null` if `multiAgentGuidanceEnabled` is `false` (`collaboration.ts:228`).
  - v1 surface: only when effort is `"max"` or `"ultra"` (`collaboration.ts:340-344`; `ultra` is normalized to `max` at `parser.ts:652`). Text is the 271-char proactive message (`collaboration.ts:131-137`).
  - v2 surface: no effort gate. If the app-server catalog state is `stale` or `unknown`, it returns the 152-char message: `<multi_agent_mode>The model catalog changed after Codex started; do not set model or reasoning_effort overrides until Codex restarts.</multi_agent_mode>` (`collaboration.ts:248-252`). Otherwise it can return the model-designation/roster text (650 chars observed) when `injectionModel` or a resolvable roster is present (`collaboration.ts:256-337`).
- The live config has `multiAgentGuidanceEnabled: true`, `injectionModel: null`, `injectionEffort: null`, `injectionPrompt: null`, and five configured `subagentModels` (`~/.opencodex/config.json`; `/api/injection-model`). The observed current injections are all v2, 152-char stale-catalog messages, meaning `collectCodexAppServerCatalogState()` is returning `stale`/`unknown` (`collaboration.ts:248-252`).

The bug specifically manifests for stateless HTTP clients: they send full conversation history on every turn and do not send `previous_response_id`. It is not provider- or model-specific; logs show the same behavior for routed `deepseek-v4-flash` and `GLM-5.2` requests.

## Why dedup fails for stateless clients

1. `parseRequest()` obtains the replay-prefix length from `previousResponseReplayPrefixLength(body)` (`parser.ts:296-298`).
2. That length is only nonzero when `expandPreviousResponseInput()` actually expanded a `previous_response_id` into a full input replay (`state.ts:843-863`; `replayedInputPrefixLengths` set at `state.ts:861`).
3. `parseRequest()` only emits `_replayPrefixLen` when the value is `> 0` (`parser.ts:685`). For stateless requests it is absent entirely, so `parsed._replayPrefixLen ?? 0` is `0`.
4. `injectDeveloperMessage()` dedups by scanning only the replayed prefix:

```ts
// collaboration.ts:392-395
const replayPrefixLen = Math.min(parsed._replayPrefixLen ?? 0, raw.input.length);
if (raw.input.slice(0, replayPrefixLen).some(item => isGeneratedDeveloperItem(item, text))) {
  return;
}
```

With `replayPrefixLen = 0`, `raw.input.slice(0, 0)` is `[]`, so `.some(...)` is always `false` and a fresh copy is appended (`collaboration.ts:398-408`).

Even when `_replayPrefixLen > 0`, the scan is limited to the locally expanded prefix; it does not scan the whole input. A client that resends a previously injected copy anywhere else in the input (including one injected during an earlier stateless turn) will still get another copy appended.

## Where each injected copy lands and whether it persists

`injectDeveloperMessage()` does two things (`collaboration.ts:388-408`):

- Pushes `{role: "developer", content: text}` into `parsed.context.messages` for routed providers (`collaboration.ts:398`).
- Pushes the wire item `{type:"message", role:"developer", content:[{type:"input_text", text}]}` onto `raw.input` — or inserts it before `compaction_trigger` when present (`collaboration.ts:399-408`). Since `_rawBody` is the same object forwarded to native passthrough, the upstream request carries it.

The proxy also persists it indirectly: `rememberResponseState()` stores `[...inputItems(request.input), ...response.output]` keyed by `response.id` (`state.ts:979-988`). That is why spill entries contain one or more injected developer messages, always somewhere inside the stored input/output sequence.

Evidence from `~/.opencodex/responses-state-spill/` at audit time:

- 201 spill files total; 98 contain `<multi_agent_mode>`.
- 97 well-formed spill objects contained 183 guidance developer-message copies: average 1.89 copies per spill, max 2, average item count 453, average latest-copy position 400, max latest position 918.
- Sample `560f2f33-...-319776.spill.json` (137 items): guidance at positions 55 (271-char proactive text, older) and 133 (152-char stale-catalog text, newer). The newest copy is near the end, followed by the turn's output.
- Sample `32f6f654-...-171058.spill.json`: guidance at positions 185 (proactive) and 215 (stale-catalog), again near the end.
- Older spill generations (e.g. `f6d2fbd0-...-1655694.spill.json`, 638 items) contain a single 271-char proactive copy at position 112, consistent with accumulation starting earlier.

The copies persist because the client keeps the conversation and resends it: during this audit the exact 152-char stale-catalog string arrived repeatedly in the parent/child conversation stream as user-role messages. That string is byte-for-byte the text generated at `collaboration.ts:250-251`. The proxy's cache is not the only persistence mechanism; client-side history retention is what makes the duplication visible in the conversation.

## Response-state cache semantics

- Store: `Map<string, StoredResponseState>` in `state.ts:67`, keyed by `response.id` (`state.ts:979-988`). It is not keyed by conversation or thread.
- It is only consulted when a request has `previous_response_id` (`expandPreviousResponseInput()`, `state.ts:843-863`).
- Limits (all hard-coded, not configurable):
  - `MAX_STORED_RESPONSES = 1_000` (`state.ts:17`)
  - `RESPONSE_TTL_MS = 60 * 60 * 1000` (1 hour) (`state.ts:18`)
  - `MAX_STORED_RESPONSE_BYTES = 64 MiB` (`state.ts:23`)
  - single-spill payload cap `MAX_RESPONSE_SPILL_PAYLOAD_BYTES = 256 MiB` (`spill-store.ts:55`)
- There is no config field or env var for these limits; only test-only overrides exist (`setResponseStateByteCapForTests`, `setResponseSpillPayloadCapForTests`).
- Live metrics from `/api/system/memory` (PID 1412890): `count: 226`, `residentCount: 83`, `spillStubCount: 143`, `totalBytes ≈ 63 MiB`, `spillPayloadBytes ≈ 146 MiB`, `oldestAgeMs ≈ 59.5 min`, `spillWrites: 1918`.

Because the cache is response-ID-keyed and only read for `previous_response_id`, a warm cache does not help a stateless client. It does not provide per-conversation injection state, and it cannot be tuned to do so.

## Config/env options relevant to re-injection

- `multiAgentGuidanceEnabled` is the only guidance on/off switch:
  - Schema: `config.ts:1136`; effective helper: `config.ts:2938-2942`; default `true`: `config.ts:2966`.
  - Admin API: `GET/PUT /api/injection-model` (`agent-settings-routes.ts:420-529`).
  - CLI: `ocx agent injection set --guidance off` (`agent.ts:16-23`, `agent.ts:46-69`).
- No environment variable controls guidance injection. `OCX_INJECTION_DEBUG=1` (or the runtime `/api/debug` toggle) only enables the injection debug log (`debug-settings.ts:15`, `debug-settings.ts:59-62`; log ring at `injection-debug-log.ts:12-35`).
- No config/env controls response-state cache size, TTL, or keying.

Immediate mitigation: set `multiAgentGuidanceEnabled: false` in `~/.opencodex/config.json` or via `ocx agent injection set --guidance off` / `PUT /api/injection-model {"multiAgentGuidanceEnabled": false}`. This stops new copies; it does not remove copies already stored in client history or spill files.

## Recommended fixes

1. **Full-input dedup (smallest, safest change).** Change `injectDeveloperMessage()` to scan the entire `raw.input` (not `slice(0, replayPrefixLen)`) for an identical `isGeneratedDeveloperItem(item, text)` and return early if found. This fixes stateless clients that resend full history and also makes dedup independent of the replay cache.
   - Tradeoffs: O(n) scan per collab request (small in practice); still misses clients that send only deltas; exact-text matching means a changed guidance text (roster/model/catalog-state change) could introduce a second variant alongside the old one.

2. **Per-conversation/thread injection marker independent of the replay cache.** Track a small, TTL-bounded set of injected guidance fingerprints keyed by a stable client/thread identity (e.g. `x-codex-parent-thread-id`, already read at `core.ts:1413-1414`, or provider conversation ID), and inject only once per identity+guidance fingerprint.
   - Tradeoffs: requires a stable key; missing header must fall back to full-input scan; in-memory state needs eviction; if config/guidance text changes, the marker must be fingerprint-versioned or old markers must be replaced.

3. **Replace, not append.** When a proxy-generated `<multi_agent_mode>` developer item already exists anywhere in the input, replace the old item with the current text instead of appending a new one.
   - Tradeoffs: keeps the conversation clean and handles guidance text changes; relies on reliable recognition of proxy-generated items (already implemented in `isGeneratedDeveloperItem`, `collaboration.ts:381-386`); must be careful not to rewrite a user-supplied identical string.

4. **Do not couple injection state to `previous_response_id`.** The response-state cache should remain a replay mechanism only; injection dedup should use its own marker (options 1-3). Keeping the current coupling guarantees the stateless path is never deduped.

5. **Default-off or opt-in for stateless HTTP surfaces.** If the stale/unknown catalog message is not essential for routed providers, suppress v2 guidance when there is no replay state. This stops the immediate damage but loses intended guidance for clients that need it.

Recommended combination: full-input scan + replace of proxy-generated guidance, plus an optional per-thread marker for clients that send deltas rather than full history.

## Evidence list

- Source:
  - `opencodex/src/server/responses/collaboration.ts:145-166` (surface detection), `223-345` (guidance text), `381-408` (dedup and injection).
  - `opencodex/src/server/responses/core.ts:967-984` (per-turn injection call), `1380` (replay expansion before parse), `1413-1414` (thread header read).
  - `opencodex/src/responses/parser.ts:296-298`, `344-353`, `618-631`, `652`, `685` (replay prefix length and tool merging).
  - `opencodex/src/responses/state.ts:17-26`, `67`, `843-863`, `871-874`, `950-991` (cache limits, keying, expansion).
  - `opencodex/src/responses/spill-store.ts:24`, `55`, `139-154` (spill directory and payload cap).
  - `opencodex/src/config.ts:1136`, `2938-2942`, `2966` (guidance config).
  - `opencodex/src/server/management/agent-settings-routes.ts:420-529` and `opencodex/src/cli/agent.ts:16-23`, `46-69` (disable path).
  - `opencodex/src/lib/debug-settings.ts:12-17`, `59-62` and `opencodex/src/lib/injection-debug-log.ts:12-35` (debug log).
- Runtime logs:
  - `~/.opencodex/service.log`: 1874 `multi-agent guidance injected` lines; 1156 with 152-char text, 718 with 650-char text; 517 `guidance silent` lines. Examples: lines 12927-12932 (`GLM-5.2`, 650 chars) and 15293-15376 (`deepseek-v4-flash`, 152 chars).
  - Live `/api/debug/injection-logs?limit=6`: seq 2003-2008, timestamps 2026-08-09T18:41:40Z through 18:42:37Z, all v2/152-char injections.
- Runtime state:
  - `~/.opencodex/config.json`: `multiAgentGuidanceEnabled: true`, `subagentModels` set, port 10109.
  - `/api/debug`: injection log runtime override `true`.
  - `/api/injection-model`: `multiAgentGuidanceEnabled: true`, model/effort/prompt `null`.
  - `/api/system/memory`: response-state metrics as listed above.
- Spill evidence:
  - 98 of 201 `~/.opencodex/responses-state-spill/*.spill.json` files contain `<multi_agent_mode>`; 97 well-formed files contain 183 guidance copies (avg 1.89, max 2), latest copies consistently near the end of each stored input+output sequence.
- Live conversation evidence:
  - The exact 152-char string from `collaboration.ts:250-251` arrived repeatedly as user-role messages in this audit thread, demonstrating that the client-visible parent conversation is accumulating and replaying the injected note.

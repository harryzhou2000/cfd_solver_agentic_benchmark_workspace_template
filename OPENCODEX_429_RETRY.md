# opencodex 429 wait-and-retry layer — research & plan

Status: research (manager branch). Submodule: `opencodex` @ `1adad357` (tag v2.8.0),
identical to the installed `/usr/lib/node_modules/@bitkyc08/opencodex` (v2.8.0).

> **Implemented** — commit `667ad08f` (opencodex submodule, branch `feat/429-same-target-retry`,
> draft PR [lidge-jun/opencodex#865](https://github.com/lidge-jun/opencodex/pull/865)).
> This document predates the implementation; the shipped design record lives in
> `opencodex/devlog/_plan/260802_429_same_target_retry/`. Claims below that differ from the
> devlog are superseded by it.

## Findings

- **Issue [lidge-jun/opencodex#487](https://github.com/lidge-jun/opencodex/issues/487)**
  "[Feature]: Optional same-target retry (configurable interval) before failover on 429" is
  exactly this feature request. It was **auto-closed as `not_planned`** by the issue-quality bot
  for missing structured detail; the author offered to open a PR, but none exists.
- **PR #514 (merged)** — `fix(proxy): attach Retry-After on retryable 429s for Codex` is what
  v2.8.0 already ships: 429s are forwarded to the client with `Retry-After`
  (`src/server/claude-messages.ts:722`, `src/server/chat-completions.ts:193`).
- **PR #585 (merged)** — same-request multi-account failover on quota 429 (`responses/core.ts`).
- **Codex client does not retry 429** (it retries 5xx only; per #487 citing openai/codex#30471),
  so a proxy-side retry is the only place this can be fixed.
- v2.8.0 has **no wait-and-retry on the same key**: the only 429 retry is multi-key rotation
  (`src/providers/key-failover.ts`), which requires ≥2 keys in `apiKeyPool`. BLSC has 1 key, so
  its 429s surface to Codex immediately and the turn dies.

## Current 429 flow (v2.8.0)

1. `src/server/responses/core.ts` `handleResponses` — the recovery `for(;;)` loop
   (`recovery:` label, ~line 2160) handles pre-stream upstream errors. The multi-key 429
   failover is a `while (status === 429 && hasKeyPoolFailover(...))` loop inside it (~line 2200);
   single-key pools no-op there, and the 429 falls through to the final
   `formatErrorResponse(429, "rate_limit_error", ...)` (~line 1266).
2. `src/server/chat-completions.ts` reuses `handleResponses` for the upstream call
   (`const upstream = await handleResponses(...)`, ~line 150), so the Responses insertion point
   covers both `/v1/responses` and `/v1/chat/completions`.
3. `src/server/claude-messages.ts` routes through `handleResponses` (~line 721), so the same
   insertion point also covers routed `/v1/messages`; only the native Anthropic passthrough
   branch bypasses the recovery loop and is out of scope for this feature.
4. `src/providers/key-failover.ts` owns key rotation + in-memory cooldown (default 60 s,
   cap 10 min, honors `Retry-After`).

## Design

Provider-level opt-in policy (schema: `providerConfigSchema` in `src/config.ts:434`):

```jsonc
// ~/.opencodex/config.json → providers.BLSC
"retryOn429": {
  "enabled": true,
  "attempts": 5,            // extra same-target attempts after the first 429
  "intervalMs": 5000,       // fixed delay between attempts
  "maxIntervalMs": 60000,   // cap for a far-future Retry-After
  "respectRetryAfter": true // prefer upstream Retry-After when present
}
```

Default disabled → zero behavior change for existing setups.

Insertion point: inside the pre-stream recovery loop in `responses/core.ts`, after the OAuth-401
replay branch and **before** the multi-key failover `while`, so "primary-first" users keep the
same key/target on rate-limit blips and only fail over after retries are exhausted:

```ts
let rateLimitRetries = 0;
const policy = retryOn429Policy(route.provider); // reads provider config
while (upstreamResponse.status === 429 && policy && rateLimitRetries < policy.attempts) {
  rateLimitRetries++;
  const retryAfter = upstreamResponse.headers.get("retry-after");
  const delay = policy.respectRetryAfter && retryAfter
    ? Math.min(parseRetryAfterMs(retryAfter) ?? policy.intervalMs, policy.maxIntervalMs)
    : policy.intervalMs; // fixed interval is used as-is (schema-bounded)
  await abortableSleep(delay, options.abortSignal);
  try { void upstreamResponse.body?.cancel().catch(() => {}); } catch {}
  const result = await rebuildAndRefetch("rate-limit-429"); // existing helper, same key
  if ("failed" in result) return result.failed;
  upstreamResponse = result;
}
```

`rebuildAndRefetch` (defined at `responses/core.ts:2132`) re-serializes the request from `parsed`,
so the replay is byte-equivalent and safe: 429 arrives pre-stream, before any bytes reach the
client. The retry budget is declared outside the recovery loop so 413/401 replays cannot re-arm
it. After retries are exhausted the existing key-failover and error mapping run unchanged. The
policy applies to key-auth providers only; OAuth/forward credentials are never replayed.

## Edge cases

- Abort during the wait: `abortableSleep` must reject on `options.abortSignal` so client cancel
  stays instant (pattern already used in `src/lib/abort.ts`).
- Far-future `Retry-After`: cap with `maxIntervalMs`; malformed values fall back to `intervalMs`.
- Concurrent requests on a single key can retry simultaneously — acceptable for a single-user
  proxy; a shared per-key retry cooldown can serialize later if needed.
- Total added latency bound: worst case `attempts × maxIntervalMs` (e.g. 5 × 60 s = 300 s) when
  honoring upstream `Retry-After`; `attempts × intervalMs` (e.g. 5 × 5 s = 25 s) when
  `respectRetryAfter=false` or no header is present.
- Streaming: only the pre-stream fetch is retried; a 429 can never arrive mid-stream, so replay
  is lossless.

## Test plan

1. Unit: policy normalization + delay computation (seconds, HTTP-date, `0`, malformed, cap) —
   shipped in `tests/rate-limit-retry.test.ts`.
2. Unit: attempts exhausted → existing key-failover still runs (e2e ordering test); recovery
   kind survives persisted usage logs (`tests/usage-log.test.ts`).
3. E2E: single-key replay to success, immediate passthrough without the knob, exhaustion, and
   retry-before-failover ordering — shipped in `tests/server-rate-limit-retry-e2e.test.ts`.
4. E2E: client abort during sleep returns 499 with no further upstream sends — shipped.
5. Manual (not shipped): BLSC single key under real load.

## Shipping options

- Fork `lidge-jun/opencodex`, branch off `dev`, implement, test, open PR referencing #487 —
  done: draft PR #865 (branch `feat/429-same-target-retry`).
- Or keep ocx untouched and extend `ocx-relay` (`deepseek-relay.mjs`) with a `--retry-429` mode
  (wait `Retry-After`/fixed backoff, replay, max attempts) and point the provider `baseUrl` at it.

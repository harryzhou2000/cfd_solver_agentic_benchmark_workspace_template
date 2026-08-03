# opencode session token & cache history

Where opencode keeps session state, what the schema means, and how to compute
cache hit rates — including subagent sessions — without the TUI or server.

## Data locations

| Data | Path | Format |
| --- | --- | --- |
| Session store | `~/.local/share/opencode/opencode.db` (+ `-wal`, `-shm`) | SQLite; sessions, messages, parts |
| opencode data dir | `~/.local/share/opencode/` (override: `OPENCODE_DATA` / `XDG_DATA_HOME`) | storage, logs |
| Config | `~/.config/opencode/opencode.jsonc` | models, agents, plugins |
| CLI views | `opencode session list`, `opencode stats`, `opencode export <sessionID>` | official commands; `export`/`stats` can fail if the CLI cannot open its log — direct DB reads below are the reliable path |

## Schema essentials

- `session` — one row per opencode session: `id` (`ses_…`), `parent_id`
  (**subagent link**: child sessions point at the orchestrator that spawned
  them), `agent` (orchestrator / fixer / oracle / …), `model`, `directory`,
  `time_created`, and aggregate token columns (`tokens_input`,
  `tokens_output`, `tokens_cache_read`, `tokens_cache_write`, `cost`).
- `message` — one row per message: `session_id`, `time_created`, `data` JSON.
  Assistant rows carry the per-request accounting; user rows do not.
- `part` — steps/text/tool calls within a message. **Synthetic subagent
  summary parts live here**, not in `message`; they carry no usage tokens.

## Per-request usage semantics (`message.data`)

For `role: "assistant"` messages the `data` JSON has:

```json
{ "role": "assistant", "agent": "fixer", "modelID": "deepseek-v4-flash",
  "providerID": "deepseek", "finish": "tool-calls", "cost": 0.00095,
  "tokens": { "total": 239067, "input": 169, "output": 927,
              "reasoning": 19, "cache": { "write": 0, "read": 237952 } } }
```

Critical detail: **`tokens.input` is the non-cached (miss) prompt portion**,
and `tokens.cache.read` is the cached prefix. So:

- `prompt = input + cache.read + cache.write`
- `hit rate = cache.read / prompt`
- sanity check: `total = input + output + reasoning + cache.read + cache.write`

This differs from the ocx/codex usage log, where `inputTokens` already
includes the cached tokens.

## Subagent extraction

Subagents are sessions with `parent_id = <parent session id>`, recursively
(fixers/oracles spawned by the orchestrator; children can spawn their own).
`agent` labels the role. The script walks the whole tree:

```bash
node scripts/opencode_session_cache_stats.mjs --session ses_<id> --subagents
```

Output: head/middle/tail/all windows for the main session, one line per
descendant session (id, agent, model, requests, in_new, cached, hit %, stub),
and a combined window over main + subagents. `--json` emits the full object
(`all`, `hourly`, `perModel`, `subagents`, `combined`).

## Reading the numbers

- HEAD: first requests are the warm-up; expect ~0% on request 1, then
  ≥95% by request ~5–10 for a provider with working prefix caching.
- TAIL / ALL: steady state; ≥95% is healthy, ≥99% excellent.
- `output<=1, cache.read=0` rows are stub-accounting placeholders
  (unmeasurable, not misses); the report counts them separately as `stub`.
- `cache.read=0` with real output is a genuine miss (context change,
  compaction, per-turn synthetic injections, or provider cache loss) —
  listed under anomalies as `real request with zero cache`.

## Recipe

1. Find the session: `node scripts/opencode_session_cache_stats.mjs --workspace <dir-substring>`
   (lists sessions whose `directory` matches, picks the latest with messages).
2. Analyze the orchestrator alone: `--session ses_<id>`.
3. Add `--subagents` to include and break down the fixer/oracle children.
4. Cross-check against the provider probes (`probe_model_cache.js`,
   `probe_tool_turns.js`) when a session looks anomalous — the provider may
   be misreporting rather than the session misbehaving.

## Caveats

- The DB is WAL-mode; read-only queries are safe while the TUI/server runs.
- `session.tokens_*` columns are aggregate sums over that session's
  messages — useful as a cross-check, not a source of per-request curves.
- Requests that failed or returned no usage appear as assistant messages
  without tokens (counted in `noUsage`), not as cache misses.
- Timestamps are epoch milliseconds (`time_created`).

## Plugin system-prompt mutation (the goal-plugin pitfall)

`@prevalentware/opencode-goal-plugin` (loaded globally via
`~/.opencode/opencode.json`) injects a per-request-changing "goal mode active
reminder" into the **system prompt** of every request while the session has an
active goal. The reminder ends with volatile budget counters:

```text
- Time spent pursuing goal: 66 seconds   <- changes every request
- Tokens used: 8718                       <- changes every request
- Auto-continues used: 0/25               <- changes on auto-continue
```

Because DeepSeek prefix caching is byte-exact, the cache breaks at the first
changed number (a fixed position), so `cache.read` **pins at a constant**
value ≈ system-prompt-before-reminder while `in_new` carries the full history
every request. Sessions without an active goal (including all subagent
children, which have no goal entry) cache normally (95–99.6%).

Diagnosis steps:

1. Scanner: `node scripts/opencode_session_cache_stats.mjs --session <id>`
   — look for a flat `cache.read` plateau with growing `in_new` (the
   "persistent low plateau" signature).
2. Goal state: check `~/.local/share/opencode-goal-plugin/goals.json` (or
   `OPENCODE_GOAL_STATE_PATH`) for an entry with `status: "active"` for the
   session ID.
3. Wire proof (optional):
   `node scripts/opencode_wire_proxy.mjs 8791 /tmp/oc-wire api.deepseek.com /v1`,
   point a minimal opencode config at `http://127.0.0.1:8791`, seed a goal via
   `OPENCODE_GOAL_STATE_PATH`, and diff consecutive `NNN_req.txt` system
   prompts — the only diffs will be the budget numbers.

Environment gotcha: if `HTTP_PROXY`/`HTTPS_PROXY` are set, opencode's fetch
routes even `127.0.0.1` through the LAN proxy (silent 503, nothing logged).
Unset the proxy vars when running the wire proxy.

# Metadata Specification

## Purpose

Record execution **metadata** (distinct from measured expenses and from
reviewer scores) so a contestant run can be reproduced and audited: what
harness ran it, which models (including subagents) with which reasoning
modes, how much context was used, how subagents were structured, what the
prompt history was, and — for codex runs that are not a full vanilla model
set — which opencodex router version/config was in play.

The metadata is collected by the evaluation agent via
`evaluation/tools/extract_metadata.py` and embedded in `summary.json` under
`metadata` (schema: `evaluation/schemas/summary.schema.json`).

All execution metadata and captured run-time configuration comes exclusively
from `<workspace>/.sessions/`. External path overrides are rejected and the
evaluator account's state is never queried. A missing local database, rollout,
history, catalog, or config is recorded as unavailable; it never triggers a
fallback. The evaluator manually confirms `--harness` and, for Codex, the
primary `--roots`, including Docker runs whose recorded cwd is `/workspace`.

## 1. Harness metadata

From each root session's bundled `session_meta` record plus captured files
beneath `.sessions/codex/`:

- `cli_version` (e.g. `0.146.0`), `originator` (`codex-tui`, `codex-exec`,
  `app-server`, ...), `source`, `thread_source`, `multi_agent_version`,
  `history_mode`, `memory_mode`, `model_provider`.
- Codex may encode `source` or `thread_source` as a tagged object for a
  spawned continuation. Store the scalar union tag (for example `subagent`)
  in the ordinary field and retain the complete immutable object in the
  corresponding `*_details` field.
- `codex_runtime_version`: the selected codex version recorded in bundled
  `codex-runtime.json` / `version.json`, when present.
- `plugins`: captured plugin manifests beneath the bundled Codex config root,
  when present.

## 2. Model metadata (including subagents)

For every thread (root and subagent) in the contestant's session set:

- `model` (as recorded by codex, e.g. `gpt-5.6-sol`, `BLSC/GLM-5.2`).
- `entry_model` and `entry_reasoning_effort`: the first persisted
  `thread_settings_applied` values. Dashboard primary-model identity uses
  these entry values and does not change if a thread later switches model.
- `reasoning_effort`: codex-recorded effort/mode for the thread, from
  `turn_context` records and `codex.turn.reasoning_effort` usage-log lines
  (e.g. `ultra`, `high`). For router-managed models (BLSC/DeepSeek routed via
  opencodex) codex does not record an effort; this is reported as `null` with
  the router's effort map recorded in the opencodex section instead.
- Catalog metadata per model from the opencodex catalog
  (`opencodex-catalog.json`): `display_name`, `context_window`,
  `max_context_window`, supported reasoning levels, when the model is listed.
- Codex rollout token events may persist `model_context_window`; retain this
  recorded value per thread and use it when catalog metadata is unavailable.

For OpenCode, `session.model` is only the session's current/final selection;
it may change during a run. Each selected session therefore records
`entry_provider`, `entry_model`, and `entry_variant` from its first persisted
assistant request plus `usage_by_model`, aggregated from assistant-message
`providerID`, `modelID`, `variant`, token categories, and cost. This
message-level decomposition is authoritative for model attribution and
pricing. Session-row counters remain an independent whole-session total.
Never add `step-finish` part usage because it duplicates message usage.

## 3. Context window used

- `context_window` per model: the advertised window from the catalog, falling
  back to a positive persisted `model_context_window` from the selected
  thread's rollout when the model is absent from the catalog.
- `context_used`: observed context per thread and per model — the maximum
  `input_tokens` across turns, i.e. the largest context actually fed to the
  model, and the mean input tokens per turn. Prefer usage-log rows; when those
  are unavailable, derive both values from rollout token events'
  `last_token_usage.input_tokens`.

## 4. Subagent calls / threads

For each subagent thread (linked via `thread_spawn_edges`):

- parent thread, `agent_nickname`, `agent_path` (the task path, e.g.
  `/root/mesh_analysis` — used as the subagent **type**; the last path
  segment is the categorization, reviewer-adjustable), `thread_source`,
  model, reasoning effort, tokens used, timestamps.

## 5. opencodex router metadata (non-vanilla codex runs only)

Included when any thread model is **not** in the vanilla codex model set
(`gpt-5.6-sol`, `gpt-5.6-terra`, `gpt-5.6-luna`, `gpt-5.5`, `gpt-5.4`,
`gpt-5.4-mini`, `gpt-5.3-codex-spark`, `gpt-oss-*`, `codex-auto-review`):

- `opencodex_version` (installed CLI) and `opencodex_submodule_pin`
  (the `opencodex/` submodule version in this repo).
- Proxy/routing config facts from the bundled OpenCodex config:
  `defaultProvider`, provider names, `multiAgentMode`, `subagentModels`,
  `disabledModels`, `contextCapValue`, and per-provider reasoning-effort
  maps. **Credentials are never extracted** — only the whitelisted fields
  above.
- Codex-side bundled proxy fallback config presence
  (`opencodex.config.toml`, `model_catalog_json` path).

## 6. Initial prompt and resume prompts

Per root session, from `history.jsonl` (typed prompts) and
`thread_goals`:

- `goal_objective` (when the thread has a goal).
- `initial_user_prompt`: the first typed prompt of the session.
- `resume_prompts`: every subsequent typed prompt with its timestamp
  (repeated goal prompts and other resume/breakpoint prompts), so a run that
  was restarted or resumed is auditable.

Fallback: when `history.jsonl` lacks entries, the first/last non-harness
user-role messages in the session rollout are used, with harness-injected
blocks (`<codex_internal_context>`, `<environment_context>`, AGENTS.md
wrappers) excluded.

Both history and rollout inputs must be bundled beneath `.sessions/`. SQLite
`rollout_path` values from migrated or Docker runs must be rebased to the
matching `.sessions/codex/sessions/**/rollout-*.jsonl`; never follow an
original absolute path. Missing or ambiguous bundled matches remain
unavailable.

## 7. Missing metadata and classification questions

If a required metadata field cannot be extracted, the evaluation agent does
**not** guess. The extractor:

1. emits a structured entry in `metadata.questions` — `id`, `question`,
   `reason`, `suggested_source` (where the user can look it up), and
   `answer: null`;
2. sets `metadata.status` to `needs_user_input`;
3. surfaces the questions in `summary.md` under "Metadata questions for user".

The evaluation agent may ask the operator to resolve primary-harness/root
classification or to document a limitation, records the answers as
`{"<question_id>": "<answer>"}` in a JSON file, and re-runs with
`--answers <file>` (accepted by `extract_metadata.py` and `summarize.py`). An
answer does not replace missing `.sessions` evidence or authorize another data
source. `metadata.status` becomes `complete` only when all required evidence
is present locally and classification is resolved; otherwise the affected
fields remain unavailable and status remains incomplete.

Known situations that produce questions:

- **opencode harness**: when the canonical bundled database
  `.sessions/opencode-data/opencode/opencode.db` is unavailable or
  contains no sessions for the workspace (harness version, session list,
  models, prompts).
- **Router-managed models (codex)**: per-turn reasoning effort is not
  recorded by codex for non-vanilla models when the usage logs lack it; the
  value remains unavailable unless the corresponding capture or effort map is
  bundled beneath `.sessions/`.
- **Uncataloged models**: a model absent from the local model catalog has no
  context-window figure; record it as unavailable rather than consulting an
  external catalog.
- **Unreadable prompt content**: session message bodies that cannot be read
  from the bundled local data store. The extractor reports this as
  unavailable; it does not invoke an external export or search another store.

## 8. Workspace repository state (AGENTS.md, CodeGraph, benchmark submodule)

Each contestant run may use a slightly different `AGENTS.md` (per-branch
versions, local edits, or none at all), so the exact state is recorded, not
assumed:

- `workspace.agents_md`: the actual file content of `<workspace>/AGENTS.md`,
  its `sha256`, and whether it matches the workspace's git `HEAD` version
  (`matches_git_head`). A missing file is reported explicitly and raises a
  metadata question ("which AGENTS.md was in effect?"), because the harness
  injects it into the run.
- `workspace.codegraph`: whether `<workspace>/.codegraph/` exists (agents are
  instructed to use CodeGraph when present).
- `workspace.benchmark_submodule`: the `cfd_solver_agentic_benchmark`
  submodule's `commit`, `branch`, `dirty` state, and `origin_url`, so the
  exact benchmark revision under test is pinned in the record.
- `workspace.git`: the workspace repository's own `branch` and `commit` (the
  version of the workspace the run executed from).

## 9. Session timestamps

When each session started (and ended) is recorded explicitly:

- `metadata.threads.<id>.started_at` / `ended_at` (codex: rollout first/last
  event, fallback `threads.created_at`/`updated_at`) and
  `metadata.opencode.sessions[].started_at` / `ended_at` (opencode:
  `time_created`/`time_updated`).
- `metadata.session_window` — the run-wide minimum `started_at` and maximum
  `ended_at`.
- The same window is mirrored in `expenses.time_seconds.started_at` /
  `ended_at` and in `summary.contestant.session_window.{start,end}` for
  query convenience (`cfdeval query get <c> contestant.session_window.start`).

## 10. Session activity time (opencode)

For opencode runs, session **wall time includes idle periods** (user away,
API stalls, interrupted runs), so an activity time is computed from session
history instead:

- Every message carries `time.created` and (assistant messages)
  `time.completed`, so consecutive history events bracket real work
  (turns plus tool execution).
- Gaps between consecutive events longer than `idle_gap_seconds`
  (default **600 s**, CLI `--idle-gap-seconds`) are treated as interrupted
  idle time and excluded:
  `activity_time = wall_time − Σ idle gaps`.
- Recorded per session (`metadata.opencode.sessions[].activity`:
  `activity_time_seconds`, `wall_time_seconds`, `idle_time_seconds`,
  `idle_gaps`, `events`) and aggregated in
  `metadata.opencode.activity_time_seconds` /
  `idle_time_seconds` and `metadata.session_window`; mirrored in
  `expenses.time_seconds.activity_time_seconds` for opencode runs.

Validated on `omo_slim_dsv4_01`: 14 sessions, 20.5 h total wall time →
9.3 h activity (11.2 h idle excluded at the 600 s threshold).

## Output

`metadata.json` (embedded in `summary.json` as `metadata`), with sections:
`harness`, `models`, `context`, `subagents`, `opencodex` (conditional),
`opencode` (conditional), `prompts`, `questions`, `user_answers`, `status`,
`workspace`, and `provenance`.

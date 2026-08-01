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

## 1. Harness metadata

From each root session's `session_meta` record plus the local codex install:

- `cli_version` (e.g. `0.146.0`), `originator` (`codex-tui`, `codex-exec`,
  `app-server`, ...), `source`, `thread_source`, `multi_agent_version`,
  `history_mode`, `memory_mode`, `model_provider`.
- `codex_runtime_version`: the selected codex version recorded by the local
  runtime (`~/.codex/codex-runtime.json` / `version.json`).
- `plugins`: installed codex plugins (name + version) discovered under
  `~/.codex/plugins/cache/*/<name>/<version>`.

## 2. Model metadata (including subagents)

For every thread (root and subagent) in the contestant's session set:

- `model` (as recorded by codex, e.g. `gpt-5.6-sol`, `BLSC/GLM-5.2`).
- `reasoning_effort`: codex-recorded effort/mode for the thread, from
  `turn_context` records and `codex.turn.reasoning_effort` usage-log lines
  (e.g. `ultra`, `high`). For router-managed models (BLSC/DeepSeek routed via
  opencodex) codex does not record an effort; this is reported as `null` with
  the router's effort map recorded in the opencodex section instead.
- Catalog metadata per model from the opencodex catalog
  (`opencodex-catalog.json`): `display_name`, `context_window`,
  `max_context_window`, supported reasoning levels, when the model is listed.

## 3. Context window used

- `context_window` per model: the advertised window from the catalog.
- `context_used`: observed context per thread and per model — the maximum
  `input_tokens` across turns (usage logs), i.e. the largest context actually
  fed to the model, and the mean input tokens per turn.

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
- Proxy/routing config facts from `~/.opencodex/config.json`:
  `defaultProvider`, provider names, `multiAgentMode`, `subagentModels`,
  `disabledModels`, `contextCapValue`, and per-provider reasoning-effort
  maps. **Credentials are never extracted** — only the whitelisted fields
  above.
- Codex-side proxy fallback config presence
  (`~/.codex/opencodex.config.toml`, `model_catalog_json` path).

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

## 7. Missing metadata → query the user

If a required metadata field cannot be extracted, the evaluation agent does
**not** guess. The extractor:

1. emits a structured entry in `metadata.questions` — `id`, `question`,
   `reason`, `suggested_source` (where the user can look it up), and
   `answer: null`;
2. sets `metadata.status` to `needs_user_input`;
3. surfaces the questions in `summary.md` under "Metadata questions for user".

The evaluation agent then asks the user, records the answers as
`{"<question_id>": "<answer>"}` in a JSON file, and re-runs with
`--answers <file>` (accepted by `extract_metadata.py` and `summarize.py`).
Answered questions flip `metadata.status` to `complete`.

Known situations that produce questions:

- **opencode harness**: when the opencode session database is unavailable or
  contains no sessions for the workspace (harness version, session list,
  models, prompts).
- **Router-managed models (codex)**: per-turn reasoning effort is not
  recorded by codex for non-vanilla models when the usage logs lack it; the
  suggested source is the ocx-relay capture or the ocx effort map.
- **Uncataloged models**: a model absent from the local model catalog has no
  context-window figure; the user supplies it from provider docs.
- **Unreadable prompt content**: session message bodies that cannot be read
  from the local data store (`opencode export <sessionID> --sanitize` is the
  suggested source).

## Output

`metadata.json` (embedded in `summary.json` as `metadata`), with sections:
`harness`, `models`, `context`, `subagents`, `opencodex` (conditional),
`opencode` (conditional), `prompts`, `questions`, `user_answers`, `status`,
and `provenance`.

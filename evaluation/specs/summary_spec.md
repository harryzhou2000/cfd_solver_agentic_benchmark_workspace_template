# Final Result Summary — Root Specification

Status: normative for `evaluation/tools/summarize.py` and the generated outputs.

## Purpose

For each benchmark contestant execution, produce a **final result summary** that
combines quantitative execution telemetry with reviewer-assessed scores, so
contestant runs can be compared on equal footing. The summary is generated in
the `evaluation/` directory of the manager workspace and never writes into the
benchmark repository (`cfd_solver_agentic_benchmark/`) or the contestant
workspace.

## Inputs

| Input | Source | Used for |
|---|---|---|
| Contestant workspace | directory path | layout discovery, artifacts, LOC |
| Codex threads + subagent edges | `<workspace>/.sessions/codex/state_5.sqlite` (`threads`, `thread_spawn_edges`) | token totals, model per thread, session tree |
| Codex goals | `<workspace>/.sessions/codex/goals_1.sqlite` (`thread_goals`) | goal time directly (`time_used_seconds`), goal tokens |
| Codex per-turn usage logs | `<workspace>/.sessions/codex/logs_2.sqlite` (`logs`) | exact input/cached/output token splits per thread+model |
| Codex session history | `<workspace>/.sessions/codex/sessions/**/rollout-*.jsonl` | timestamps, tool usage, subagent messages, rule-violation evidence |
| OpenCode sessions | `<workspace>/.sessions/opencode-data/opencode/opencode.db` | session tree, messages, tokens, prompts, activity |
| Claude Code sessions | `<workspace>/.sessions/claude/projects/**/*.jsonl` | explicitly selected root/subagent tree, messages, tokens, tools, final prose |
| Cost metadata | `evaluation/config/cost_metadata.json` | cost estimation |
| Review points | `evaluation/config/review_points_{code,cfd,results}.json` | scorecard generation |
| Execution metadata/config | captured files beneath `<workspace>/.sessions/` | metadata record (see `metadata_spec.md`) |

The workspace `.sessions/` boundary is exclusive. External telemetry/config
overrides are rejected, evaluator-account state is never queried, and missing
local sources remain unavailable. Codex database rollout paths are rebased to
the bundled rollout JSONLs rather than followed as original absolute paths.

## Session selection

Discovery inventories every bundled candidate, including botched, abandoned,
`paused`, `blocked`, or still-`active` runs. The evaluator must manually
classify the primary harness and root/session tree; newest, largest, or
cwd-matched is not sufficient. For Docker cwd mismatches, pass the confirmed
`--harness` and `--roots` explicitly. Claude never auto-selects even a sole
candidate; multiple continuation roots also require `--terminal-root` for the
root containing the final contestant response.

The generated summary keeps them separable:

- `expenses.time_seconds.by_root_tree` lists each root session with its
  status, model, thread count, token total, and goal time.
- `--roots <thread-id,...>` (accepted by `summarize.py` and both extractors)
  restricts the pipeline to the given root session(s) and their subagent
  trees.

## Outputs

`summarize.py --workspace <dir>` writes into
`evaluation/outputs/<workspace-basename>/`:

1. `index.json` — contract manifest (schema: `index.schema.json`) listing
   every artifact with its schema and sha256 (contract:
   `evaluation/contract/README.md`).
2. `summary.json` — machine-readable final summary (schema:
   `evaluation/schemas/summary.schema.json`).
3. `summary.md` — human-readable report of the same content.
4. `expenses.json` — time / tokens / cost detail (see `expenses_spec.md`).
5. `measurements.json` — tool usage, LOC, rule-violation findings
   (see `measurements_spec.md`).
6. `metadata.json` — harness / model / context / subagent / router / prompt
   metadata (see `metadata_spec.md`).
7. `review_code.md`, `review_cfd.md`, `review_results.md` — blank scorecards
   for reviewers, one point per row with a score column.
8. `contestant_final_response.md` — exact credential-redacted terminal Claude
   prose when a completed, unambiguous response exists (unindexed Markdown;
   provenance is retained in `metadata.json`).

All JSON artifacts validate against the schemas in `evaluation/schemas/`
and are format-checked by `cfdeval check` / `tools/check_result.py`; the
`cfdeval` package (uv) also provides `query` for listing, comparing, and
dot-path access to results.

## Summary structure

```jsonc
{
  "contestant": {           // workspace, branch, commits, discovered layout
    "workspace": "...",
    "branch": "...",
    "git_commit": "...",
    "benchmark_submodule_commit": "...",
    "layout": "standard|non_standard",
    "solver_dir": "...", "results_dir": "...", "report_dir": "...",
    "session_window": { "start": "...", "end": "..." }
  },
  "expenses": {             // expenses_spec.md
    "time_seconds": { "goal_time": 0, "wall_time": 0 },
    // plus time_seconds.by_root_tree: per root session (status, tokens,
    // goal time, thread count) so botched sessions are visible and
    // separable via --roots
    "tokens": { "total": 0, "by_model": {}, "by_thread": {}, "per_turn_source": "logs|threads" },
    "cost_estimate_usd": { "total": 0, "by_model": {}, "unpriced_tokens": 0, "metadata": "..." }
  },
  "code_review":  { "points": [...], "overall_score": null },   // code_review_spec.md
  "cfd_review":   { "points": [...], "overall_score": null },   // cfd_review_spec.md
  "result_review":{ "points": [...], "overall_score": null },   // result_review_spec.md
  "measurements": {         // measurements_spec.md
    "tool_usage": { "total": 0, "by_tool": {}, "subagent_spawns": 0 },
    "loc": { "method": "git|file", "files": 0, "lines": 0, "by_extension": {} },
    "rule_violations": []
  },
  "metadata": {             // metadata_spec.md
    "harness": { "harness": "codex", "cli_version": "...", "plugins": [...] },
    "models": { "<model>": { "catalog": {...}, "reasoning_efforts_seen": [...],
                "max_context_used": 0, "threads": 0 } },
    "context": { "by_model": {...}, "by_thread": {...} },
    "subagents": [ { "thread_id": "...", "parent_thread_id": "...",
                     "nickname": "...", "type": "mesh_analysis", "model": "...",
                     "reasoning_effort": [...], "tokens_used": 0 } ],
    "opencodex": null | { "opencodex_version": "...", "config_facts": {...} },
    "workspace": { "agents_md": { "exists": true, "sha256": "...",
                   "content": "...", "matches_git_head": true },
                   "codegraph": { "exists": false },
                   "benchmark_submodule": { "commit": "...", "dirty": false } },
    "prompts": { "by_root_thread": { "<root>": { "goal_objective": "...",
                 "initial_user_prompt": {...}, "resume_prompts": [...] } } }
  }
}
```

## Review workflow

1. `summarize.py` fills everything that is automatically extractable; review
   sections start with `"overall_score": null` and every point unscored.
2. Reviewers open the generated `review_*.md` scorecards, score each point
   0–5, and note disqualification flags found.
3. Scores are recorded back into `summary.json` (or a `review_*_scored.json`
   sidecar) by hand or by a small script; the overall score is the weighted
   mean of the point scores (weights from the review-points config).

## Non-goals

- This pipeline does not modify the benchmark repository or contestant
  workspaces (read-only access only).
- The automated parts do not decide pass/fail; they produce evidence.
  Reviewer judgment is required for scores and disqualification.

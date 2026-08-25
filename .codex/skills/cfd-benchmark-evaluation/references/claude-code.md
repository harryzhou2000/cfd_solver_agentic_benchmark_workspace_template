# Claude Code evaluation telemetry

Use this reference only when the manually confirmed contestant harness is
Claude Code.

## Evidence boundary and root selection

Read only `<workspace>/.sessions/claude`. Root project transcripts normally
live at `projects/<encoded-project>/<session-id>.jsonl`; their subagents live
at `<session-id>/subagents/**/*.jsonl`. Never inspect evaluator-home `~/.claude`,
follow transcript symlinks, accept an override outside the workspace bundle,
or use `parentUuid` as session ancestry (it is message-chain ancestry).

Inventory candidate roots and manually compare initial prompts, cwd,
timestamps, terminal messages, and subagent files. Discovery never chooses a
root, even when it finds one candidate. Reject missing, unknown, or duplicate
selected root IDs and malformed selected JSONL.

Run all four sidecar extractors with the same confirmed roots:

```bash
python3 evaluation/tools/extract_sessions.py \
  --workspace <repo> --harness claude --roots <root[,continuation...]> \
  --out evaluation/outputs/<run-id>/sessions.json
python3 evaluation/tools/extract_metadata.py \
  --workspace <repo> --harness claude --roots <root[,continuation...]> \
  --out evaluation/outputs/<run-id>/metadata.json
python3 evaluation/tools/extract_expenses.py \
  --workspace <repo> --harness claude --roots <root[,continuation...]> \
  --out evaluation/outputs/<run-id>/expenses.json
python3 evaluation/tools/extract_measurements.py \
  --workspace <repo> --harness claude --roots <root[,continuation...]> \
  --out evaluation/outputs/<run-id>/measurements.json
```

The selected tree contains each selected root, its subagent JSONLs, and any
inline `isSidechain` records normalized as subagent entities. UUID-identical
inline/file copies are counted once. Usage/tool records without timestamps remain in immutable
whole-session totals but cannot enter timeline buckets.

## Accounting

Claude's `input_tokens`, `cache_read_input_tokens`, and
`cache_creation_input_tokens` are disjoint. Normalize:

```text
input            = raw input + cache read + cache creation
cached_input     = cache read
cache_write      = cache creation
non_cached_input = raw input + cache creation
total            = input + output
```

Bundle split assistant-message fragments and deduplicate tool blocks by their
persisted IDs. Attribute tokens to each persisted model plus top-level effort
pair. Unknown Claude models remain unpriced until the manager price table has
an audited entry; do not use generic fallback rates. Claude has no Codex goal
timer, so record `goal_time: null`, not zero. Do not infer reasoning effort,
context window, or provider route when the artifacts do not record them.

## Terminal response

For one selected root, `summarize.py --harness claude --roots <root>` writes
the contestant response. With continuation roots, require
`--terminal-root <selected-id>`; never choose by argument order or recency.

Select the chronologically last non-sidechain root assistant message with
persisted `stop_reason: end_turn`, concatenate ordered text blocks, ignore
tool-use turns and all subagent prose, and hash the exact raw JSONL bytes of
every contributing fragment in line order. Equal-timestamp terminal
candidates are ambiguous. If no completed
text exists, record absence. Redact only credential spans with
`[REDACTED_CREDENTIAL]` and preserve the original record hash.

Use `evaluation/tools/extract_claude_final_response.py` for standalone
extraction. Read the resulting `contestant_final_response.md` before scoring.

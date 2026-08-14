# Agent-Driven Evaluation Specification

## Purpose

The evaluation agent (a coding agent, not an automatic script) turns the
auto-extracted snapshot into scored, reported judgments. Reports and scores
live **inside the snapshot folder**, so every snapshot is self-contained.

## Artifacts

- `agent_report.md` — narrative: run summary (auto-filled), methodology,
  per-section rubric findings, review-area scores, disqualification
  assessment, metadata answers/session selection, limitations, verdict.
- `agent_scores.json` (schema: `evaluation/schemas/agent_scores.schema.json`):
  - `metadata_answers`: answers to `metadata.json.questions`;
  - `session_selection`: source + roots chosen from `sessions.json`
    discovery, with rationale, selected-run start/end timestamps, and the UTC
    execution date derived from the selected primary run's start;
  - `scores.{code_review,cfd_review,result_review}`: per-point 0-5 scores and
    notes (point ids/weights from `evaluation/config/review_points_*.json`);
  - `rubric`: the 10 sections of the benchmark's `SCORING_RUBRIC.md`
    (100 points total) with per-section scores and the computed total;
  - `case_scores`: all eight required cases, each with an independent 0-5
    score and evidence note; these scores do not contribute to the rubric or
    Code/CFD/Results review scores;
  - `disqualification`: the 13 triggers with evidence and found flags;
  - `limitations`: what could not be verified.

## Workflow

```bash
# 1. auto-extract everything
python3 evaluation/tools/summarize.py --workspace ../codex_gpt56_01

# 2. scaffold the report + scores skeleton with auto evidence
python3 evaluation/tools/generate_agent_report.py \
  --out evaluation/outputs/codex_gpt56_01 \
  --workspace ../codex_gpt56_01 --run-validator --agent gpt-5.6-terra

# 3. the agent reads evidence, asks the user for unextractable metadata,
#    scores review points + rubric, and fills agent_report.md / agent_scores.json

# 4. record + validate + re-index
python3 evaluation/tools/record_agent_results.py \
  --folder evaluation/outputs/codex_gpt56_01
uv run cfdeval check evaluation/outputs/codex_gpt56_01
```

## Rules

- Scores are judgments with evidence: every per-case score and every
  rubric/review point below full marks should carry a note pointing to the
  evidence.
- Nothing in this step writes into the contestant workspace or benchmark
  repo (read-only evaluation).
- If the agent cannot verify a score, it records `null` and explains in
  `limitations` rather than guessing.

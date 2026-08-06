#!/usr/bin/env python3
"""Scaffold the agent-driven evaluation report for a result snapshot.

Writes `<out>/agent_report.md` with auto-filled evidence sections (expenses,
measurements, metadata questions, structural result checks, session analysis
summary, rubric skeleton) and `## Assessment` placeholders that the evaluating
agent fills in, plus a matching `agent_scores.json` skeleton.

Usage:
  python3 evaluation/tools/generate_agent_report.py --out evaluation/outputs/<c>
    [--workspace ../codex_gpt56_01] [--run-validator] [--agent "gpt-5.6-terra"]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


RUBRIC_SECTIONS = [
    ("build_cli", "Build, CLI, Output Contract", 10),
    ("mesh_geometry", "Mesh And Geometry", 10),
    ("residual_bc", "Finite-Volume Residual And Boundary Conditions", 15),
    ("second_order", "Second-Order Spatial Scheme", 10),
    ("viscous", "Viscous Terms", 10),
    ("implicit_transient", "Implicit And Transient Methods", 15),
    ("mpi", "MPI", 10),
    ("case_results", "Case Results And Validation", 10),
    ("report_viz", "Report, Visualization, Analysis", 5),
    ("extensibility", "Extensibility", 5),
]


def _load(out: Path, name: str) -> dict | None:
    p = out / name
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text())
    except json.JSONDecodeError:
        return None


def _run_validator(workspace: Path, timeout: int = 900) -> dict:
    ex = workspace / "cfd_solver_agentic_benchmark" / "examiner" / "validate_outputs.py"
    if not ex.exists():
        return {"ran": False, "reason": f"validator not found at {ex}"}
    try:
        r = subprocess.run(
            [sys.executable, str(ex), "--report"],
            cwd=workspace, capture_output=True, text=True, timeout=timeout)
        tail = (r.stdout or "")[-6000:]
        return {
            "ran": True,
            "returncode": r.returncode,
            "output_tail": tail,
            "stderr_tail": (r.stderr or "")[-1000:],
        }
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"ran": False, "reason": str(exc)[:300]}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Scaffold agent evaluation report + scores")
    ap.add_argument("--out", required=True)
    ap.add_argument("--workspace", default=None)
    ap.add_argument("--run-validator", action="store_true",
                    help="run cfd_solver_agentic_benchmark/examiner/validate_outputs.py")
    ap.add_argument("--agent", default=None)
    ap.add_argument("--harness", default=None)
    args = ap.parse_args(argv)

    out = Path(args.out).resolve()
    summary = _load(out, "summary.json") or {}
    metadata = _load(out, "metadata.json") or summary.get("metadata", {})
    sessions = _load(out, "sessions.json")
    contestant = summary.get("contestant", {})
    name = contestant.get("workspace", str(out)).rstrip("/").split("/")[-1] or out.name
    ws = Path(args.workspace) if args.workspace else (
        Path(contestant["workspace"]) if contestant.get("workspace") else None)

    validator = _run_validator(ws) if (args.run_validator and ws) else None
    now = datetime.now(timezone.utc).isoformat()

    lines = [
        f"# Agent Evaluation Report — {name}",
        "",
        f"- Evaluated at: {now}",
        f"- Evaluating agent: {args.agent or 'TBD'}",
        f"- Harness: {args.harness or metadata.get('harness', {}).get('harness', '?')}",
        "",
        "## Run summary (auto)",
        "",
    ]
    if contestant:
        lines.append(
            f"- Workspace: `{contestant.get('workspace')}` branch "
            f"`{contestant.get('branch')}` commit `{contestant.get('git_commit')}`")
        lines.append(
            f"- Benchmark submodule: {contestant.get('benchmark_submodule_commit')}")
        sw = contestant.get("session_window", {})
        if sw.get("start"):
            lines.append(f"- Session window: {sw.get('start')} → {sw.get('end')}")
    e = summary.get("expenses", {})
    if e:
        t = e.get("time_seconds", {})
        lines.append(
            f"- Time: goal {t.get('goal_time', 0)}s, wall {t.get('wall_time', 0)}s"
            + (f", activity {t.get('activity_time_seconds')}s" if t.get("activity_time_seconds") else ""))
        tok = e.get("tokens", {})
        cost = e.get("cost_estimate_usd", {})
        lines.append(
            f"- Tokens: {tok.get('total', 0):,} "
            f"(main {tok.get('main_vs_subagent', {}).get('main', 0):,} / "
            f"subagents {tok.get('main_vs_subagent', {}).get('subagent', 0):,}); "
            f"cost est. ${cost.get('total', 0):.2f}")
    if metadata.get("questions"):
        lines += [
            "",
            "### Metadata questions requiring user answers",
            "",
            "| Question | Reason | Suggested source |",
            "|----------|--------|------------------|",
        ]
        for q in metadata["questions"]:
            lines.append(f"| {q['id']}: {q['question']} | {q['reason']} | "
                         f"{q['suggested_source']} |")
        lines += [
            "",
            "Record the user's answers in `agent_scores.json` under "
            "`metadata_answers`, then re-run summarize with `--answers`.",
        ]
    if sessions:
        an = sessions.get("analysis", {})
        lines += [
            "",
            "### Session analysis (auto)",
            "",
            f"- Source: {sessions.get('selected_source')} — codex "
            f"{sessions.get('codex', {}).get('thread_count', 0)} threads / "
            f"opencode {sessions.get('opencode', {}).get('session_count', 0)} sessions",
            f"- Window: {an.get('window', {}).get('start')} → "
            f"{an.get('window', {}).get('end')}",
        ]
        if an.get("idle_exclusion"):
            ix = an["idle_exclusion"]
            lines.append(
                f"- Idle excluded: {ix.get('gap_count')} gaps, "
                f"{ix.get('idle_seconds_total', 0):.0f}s "
                f"(threshold {an.get('idle_gap_seconds')}s, merged main+subagents)")
        pw = an.get("permission_waits", {})
        if pw:
            lines.append(
                f"- Permission-wait candidates: {pw.get('candidate_count')} "
                f"(method: {pw.get('method')}; ask-tool events: "
                f"{pw.get('ask_tool_events_found', 0)})")
            for lim in pw.get("limitations", [])[:3]:
                lines.append(f"  - limitation: {lim}")
        ws_ = an.get("whole_session_stats", {})
        if ws_:
            lines.append(
                f"- Whole-session: tokens {ws_.get('tokens', {}).get('total', 0):,}, "
                f"cache hit {ws_.get('cache', {}).get('hit_ratio')}, "
                f"tools {ws_.get('tools', {}).get('total')}")
    sc = (summary.get("result_review") or {}).get("structural_checks", {})
    if sc:
        lines += [
            "",
            "### Structural result checks (auto)",
            "",
            f"- Required cases: {len(sc.get('required_cases', []))}; found: "
            f"{len(sc.get('found_case_dirs', []))}; missing: "
            f"{', '.join(sc.get('missing_cases', [])) or 'none'}",
            f"- Figure manifest: {json.dumps(sc.get('figure_manifest'))[:400]}",
        ]
        for case, chk in list(sc.get("case_checks", {}).items())[:12]:
            lines.append(
                f"- `{case}`: missing={chk.get('missing')} "
                f"completed={chk.get('metadata', {}).get('completed')} "
                f"status={chk.get('metadata', {}).get('convergence_status')}")
    if validator:
        lines += [
            "",
            "### Validator output (auto)",
            "",
            f"- returncode: {validator.get('returncode')}",
            "```",
            validator.get("output_tail", "").strip(),
            "```",
        ]
    if validator is None and args.run_validator:
        lines += ["", "### Validator", "", "Validator did not run (workspace not found)."]

    lines += [
        "",
        "## Methodology",
        "",
        "> **Agent fills this in**: how the evidence above was gathered and "
        "verified (reads of source, builds/executions, mesh/report checks, "
        "MPI runs), and what was spot-checked vs assumed.",
        "",
        "## Rubric scores (100 points, SCORING_RUBRIC.md)",
        "",
        "| Section | Max | Score | Notes |",
        "|---------|----:|------:|-------|",
    ]
    for sid, title, maxp in RUBRIC_SECTIONS:
        lines.append(f"| {title} | {maxp} |  |  |")
    lines += [
        "",
        "## Review-area scores (0-5 weighted, review_*.json)",
        "",
        "| Area | Overall | Notes |",
        "|------|--------:|-------|",
        "| Code |  |  |",
        "| CFD methods |  |  |",
        "| Results |  |  |",
        "",
        "## Disqualification assessment",
        "",
        "> **Agent fills this in**: go through the 13 disqualification "
        "triggers in SCORING_RUBRIC.md with evidence for each.",
        "",
        "## Metadata answers & session selection",
        "",
        "> **Agent fills this in**: answers to metadata questions and which "
        "sessions/roots were counted (and why).",
        "",
        "## Limitations",
        "",
        "> **Agent fills this in**: what could not be verified.",
        "",
        "## Verdict",
        "",
        "> **Agent fills this in**: overall assessment and recommended next "
        "steps.",
        "",
    ]
    (out / "agent_report.md").write_text("\n".join(lines))

    # scores skeleton
    review_names = ("code_review", "cfd_review", "result_review")
    scores = {
        "schema": "agent_scores",
        "contestant": name,
        "evaluated_at": now,
        "evaluator": {"agent": args.agent, "session_id": None,
                      "harness": args.harness or metadata.get("harness", {}).get("harness"),
                      "notes": None},
        "metadata_answers": {},
        "session_selection": {
            "source": (sessions or {}).get("selected_source", "unknown"),
            "roots": (sessions or {}).get("selection", {}).get("roots"),
            "rationale": "",
        },
        "scores": {
            area: {
                "points": [
                    {"id": p.get("id"), "score": None, "notes": None}
                    for p in ((summary.get(area) or {}).get("points") or [])
                ],
                "overall_score": None,
                "disqualification_flags": [],
            }
            for area in review_names
        },
        "rubric": {
            "total_possible": sum(m for _i, _t, m in RUBRIC_SECTIONS),
            "total_scored": None,
            "sections": [
                {"id": sid, "title": title, "max_points": maxp,
                 "score": None, "notes": None}
                for sid, title, maxp in RUBRIC_SECTIONS
            ],
        },
        "disqualification": {"triggered": False, "flags": []},
        "limitations": [],
    }
    scores_path = out / "agent_scores.json"
    if not scores_path.exists():
        scores_path.write_text(json.dumps(scores, indent=2) + "\n")
    print(f"wrote {out / 'agent_report.md'}")
    print(f"wrote {scores_path} (fill in scores, then run record_agent_results.py)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

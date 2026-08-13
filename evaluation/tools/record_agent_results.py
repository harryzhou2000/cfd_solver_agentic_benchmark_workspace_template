#!/usr/bin/env python3
"""Record agent-driven evaluation results into a result snapshot.

Validates `agent_scores.json` against its schema and the review-points
configs (point ids must exist, scores 0-5), copies/refreshes the scores and
report into the snapshot, recomputes the overall scores and rubric total, and
re-records index.json.

Usage:
  python3 evaluation/tools/record_agent_results.py --folder evaluation/outputs/<c>
    [--scores <path>] [--report <path>]
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from cfdeval import recording, validation  # noqa: E402


def _weighted_overall(points: list[dict]) -> float | None:
    scored = [(p.get("weight", 1.0), p.get("score")) for p in points]
    scored = [(w, s) for w, s in scored if s is not None]
    if not scored:
        return None
    return round(sum(w * s for w, s in scored) / sum(w for w, _ in scored), 2)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Record agent evaluation scores + report")
    parser.add_argument("--folder", required=True)
    parser.add_argument("--scores", default=None)
    parser.add_argument("--report", default=None)
    args = parser.parse_args(argv)

    folder = Path(args.folder).resolve()
    scores_path = Path(args.scores) if args.scores else folder / "agent_scores.json"
    report_path = Path(args.report) if args.report else folder / "agent_report.md"
    if not scores_path.exists():
        print(f"ERROR: no agent_scores.json at {scores_path}", file=sys.stderr)
        return 2
    scores = json.loads(scores_path.read_text())

    # validate schema
    valid, errors = validation.validate_file(
        scores_path, ROOT / "schemas" / "agent_scores.schema.json")
    if not valid:
        print("ERROR: agent_scores.json does not validate:", file=sys.stderr)
        for e in errors[:15]:
            print(f"  {e}", file=sys.stderr)
        return 2

    # point ids must match the review-points configs; recompute overalls
    summary_path = folder / "summary.json"
    summary = json.loads(summary_path.read_text()) if summary_path.exists() else {}
    area_config_names = {"code_review": "code", "cfd_review": "cfd",
                         "result_review": "results"}
    for area in ("code_review", "cfd_review", "result_review"):
        cfg_name = f"review_points_{area_config_names[area]}.json"
        cfg = json.loads((ROOT / "config" / cfg_name).read_text())
        cfg_points = {p["id"]: p for p in cfg["points"]}
        agent_points = {p["id"]: p for p in scores["scores"][area]["points"]}
        merged = []
        for pid, cp in cfg_points.items():
            agent_pt = agent_points.get(pid, {})
            score = agent_pt.get("score")
            if score is not None and not (isinstance(score, (int, float)) and 0 <= score <= 5):
                print(f"ERROR: {area}.{pid} score {score!r} outside 0-5",
                      file=sys.stderr)
                return 2
            merged.append({
                "id": pid,
                "title": cp.get("title"),
                "weight": cp.get("weight", 1.0),
                "score": score,
                "notes": agent_pt.get("notes") or "",
            })
        scores["scores"][area]["points"] = merged
        overall = _weighted_overall(merged)
        recorded = scores["scores"][area].get("overall_score")
        scores["scores"][area]["overall_score"] = (
            recorded if recorded is not None else overall)

    # rubric total: section scores are rubric points; total = sum of scored
    # sections scaled to the full 100-point space
    secs = scores["rubric"]["sections"]
    scored_secs = [(s["max_points"], s["score"]) for s in secs
                   if s.get("score") is not None]
    if scored_secs:
        if sum(m for m, _ in scored_secs) == scores["rubric"]["total_possible"]:
            scores["rubric"]["total_scored"] = round(
                sum(sc for _m, sc in scored_secs), 1)
        else:
            # partial scoring: scale proportionally
            scores["rubric"]["total_scored"] = round(
                sum(sc for _m, sc in scored_secs)
                * scores["rubric"]["total_possible"]
                / sum(m for m, _ in scored_secs), 1)

    scores["evaluated_at"] = scores.get("evaluated_at") or datetime.now(timezone.utc).isoformat()
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "agent_scores.json").write_text(json.dumps(scores, indent=2) + "\n")
    if report_path.exists():
        dest = folder / "agent_report.md"
        if report_path.resolve() != dest.resolve():
            dest.write_text(report_path.read_text())
        print(f"wrote {dest}")
    else:
        print("no agent_report.md provided; skipped", file=sys.stderr)

    # refresh review_*.json sidecars so summary.md renders scores
    import subprocess
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "generate_review_forms.py"),
         "--out", str(folder), "--scores",
         json.dumps({area: scores["scores"][area] for area in scores["scores"]})],
        capture_output=True, text=True)
    if r.returncode != 0:
        print(f"WARNING: review forms refresh failed: {r.stderr[:300]}",
              file=sys.stderr)

    # refresh summary.json review sections + summary.md
    if summary_path.exists():
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "summarize_tool", ROOT / "tools" / "summarize.py")
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for area in ("code_review", "cfd_review", "result_review"):
            review = json.loads(
                (folder / f"review_{area_config_names[area]}.json").read_text())
            summary[area] = review
        # Sidecars are authoritative and may have been regenerated after the
        # initial summary (for example, legacy session-accounting repairs).
        for key in ("metadata", "expenses", "measurements"):
            sidecar = folder / f"{key}.json"
            if sidecar.exists():
                summary[key] = json.loads(sidecar.read_text())
        (summary_path).write_text(json.dumps(summary, indent=2) + "\n")
        mod.render_md(folder / "summary.md", summary, folder)
        print(f"refreshed {summary_path} and summary.md with recorded scores")

    # re-index
    if (folder / "summary.json").exists():
        index_artifacts = {
            "index.json": "index.schema.json",
            "summary.json": "summary.schema.json",
            "metadata.json": "metadata.schema.json",
            "expenses.json": "expenses.schema.json",
            "measurements.json": "measurements.schema.json",
            "configs.json": "configs.schema.json",
            "sessions.json": "sessions.schema.json",
            "env_snapshot.json": "env_snapshot.schema.json",
            "agent_scores.json": "agent_scores.schema.json",
            "review_code.json": "review.schema.json",
            "review_cfd.json": "review.schema.json",
            "review_results.json": "review.schema.json",
        }
        existing = {n: s for n, s in index_artifacts.items()
                    if (folder / n).exists()}
        recording.write_index(
            folder, scores["contestant"], existing,
            tools=["summarize.py", "extract_configs.py", "extract_sessions.py",
                   "generate_agent_report.py", "record_agent_results.py"],
            schema_dir=ROOT / "schemas")
        print(f"wrote {folder / 'index.json'}")
    print("agent scores recorded; validate with: "
          "uv run cfdeval check " + str(folder))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

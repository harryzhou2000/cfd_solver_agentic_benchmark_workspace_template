#!/usr/bin/env python3
"""Generate blank review scorecards (markdown + JSON skeletons) from the
review-points configs.

Usage:
  python3 evaluation/tools/generate_review_forms.py --out <dir>
    [--scores '{"code_review": {...}, ...}']   # fill scores into the JSON
                                                # sidecars (from record_agent_results.py)

Specs: evaluation/specs/{code_review,cfd_review,result_review}_spec.md
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def scorecard_md(cfg: dict, scores: dict | None = None) -> str:
    by_id = {p["id"]: p for p in (scores or {}).get("points", [])} if scores else {}
    overall = (scores or {}).get("overall_score")
    lines = [
        f"# {cfg['title']}",
        "",
        f"Score scale: {cfg['score_scale']}.",
        "Overall score = weighted mean of point scores (weights shown).",
        "Generated: " + datetime.now(timezone.utc).isoformat(),
        (f"\n**Overall score: {overall}**\n" if overall is not None else ""),
        "",
        "| # | Point | Weight | Score (0-5) | Evidence / notes |",
        "|---|-------|--------|--------------|------------------|",
    ]
    for i, p in enumerate(cfg["points"], 1):
        score = by_id.get(p["id"], {}).get("score")
        notes = by_id.get(p["id"], {}).get("notes") or ""
        score_str = "" if score is None else str(score)
        lines.append(
            f"| {i} | **{p['id']}** {p['title']} | {p['weight']:.2f} | "
            f"{score_str} | {notes} |"
        )
        lines.append(
            f"|   | {p['guidance']} | | | |"
        )
        lines.append(
            f"|   | *Evidence:* {p['evidence']} | | | |"
        )
    lines += ["", "## Disqualification flags", ""]
    flags = (scores or {}).get("disqualification_flags")
    for i, flag in enumerate(cfg.get("disqualification_flags", []), 1):
        checked = " "
        if flags:
            for f in flags:
                if isinstance(f, dict) and f.get("flag") == flag:
                    checked = "x" if f.get("triggered") else " "
                    break
        lines.append(f"- [{checked}] {flag}")
    lines += [
        "",
        "## Summary",
        "",
        f"- Overall score (0-5): {overall if overall is not None else '____'}",
        "- Key strengths:",
        "- Key weaknesses:",
        "- Disqualification triggered? yes/no — explain:",
    ]
    return "\n".join(lines) + "\n"


def skeleton_json(cfg: dict) -> dict:
    return {
        "review_area": cfg["review_area"],
        "title": cfg["title"],
        "points": [
            {"id": p["id"], "title": p["title"], "weight": p["weight"],
             "score": None, "notes": ""}
            for p in cfg["points"]
        ],
        "disqualification_flags": [
            {"flag": f, "triggered": None, "notes": ""}
            for f in cfg.get("disqualification_flags", [])
        ],
        "overall_score": None,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate blank review scorecards")
    ap.add_argument("--out", required=True)
    ap.add_argument("--scores", default=None,
                    help="JSON object mapping review area -> {points:[{id,score,notes}], "
                         "overall_score, disqualification_flags}")
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1] / "config"
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    scores = json.loads(args.scores) if args.scores else {}
    for name in ("code", "cfd", "results"):
        cfg = json.loads((root / f"review_points_{name}.json").read_text())
        skel = skeleton_json(cfg)
        area = cfg["review_area"]
        area_scores = scores.get(area)
        if area_scores:
            by_id = {p["id"]: p for p in area_scores.get("points", [])}
            for p in skel["points"]:
                sp = by_id.get(p["id"], {})
                p["score"] = sp.get("score")
                p["notes"] = sp.get("notes") or p["notes"]
            if area_scores.get("overall_score") is not None:
                skel["overall_score"] = area_scores["overall_score"]
            if area_scores.get("disqualification_flags"):
                skel["disqualification_flags"] = area_scores["disqualification_flags"]
        (out / f"review_{name}.md").write_text(scorecard_md(cfg, area_scores))
        (out / f"review_{name}.json").write_text(json.dumps(skel, indent=2) + "\n")
        print(f"wrote {out / f'review_{name}.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

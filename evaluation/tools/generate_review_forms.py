#!/usr/bin/env python3
"""Generate blank review scorecards (markdown + JSON skeletons) from the
review-points configs.

Usage:
  python3 evaluation/tools/generate_review_forms.py --out <dir>

Specs: evaluation/specs/{code_review,cfd_review,result_review}_spec.md
"""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def scorecard_md(cfg: dict) -> str:
    lines = [
        f"# {cfg['title']}",
        "",
        f"Score scale: {cfg['score_scale']}.",
        "Overall score = weighted mean of point scores (weights shown).",
        "Generated: " + datetime.now(timezone.utc).isoformat(),
        "",
        "| # | Point | Weight | Score (0-5) | Evidence / notes |",
        "|---|-------|--------|--------------|------------------|",
    ]
    for i, p in enumerate(cfg["points"], 1):
        lines.append(
            f"| {i} | **{p['id']}** {p['title']} | {p['weight']:.2f} |  |  |"
        )
        lines.append(
            f"|   | {p['guidance']} | | | |"
        )
        lines.append(
            f"|   | *Evidence:* {p['evidence']} | | | |"
        )
    lines += ["", "## Disqualification flags", ""]
    for i, flag in enumerate(cfg.get("disqualification_flags", []), 1):
        lines.append(f"- [ ] {flag}")
    lines += [
        "",
        "## Summary",
        "",
        "- Overall score (0-5): ____",
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
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1] / "config"
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name in ("code", "cfd", "results"):
        cfg = json.loads((root / f"review_points_{name}.json").read_text())
        (out / f"review_{name}.md").write_text(scorecard_md(cfg))
        (out / f"review_{name}.json").write_text(json.dumps(skeleton_json(cfg), indent=2) + "\n")
        print(f"wrote {out / f'review_{name}.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Flag explicit numeric report/scorecard disagreements for manual review.

This is a limited text screen, not a completeness or correctness check. It
does not modify snapshots and cannot decide which of two values is correct.
"""

from __future__ import annotations

import argparse
import json
import re
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1] / "outputs"
AREAS = {"code": "code_review", "cfd": "cfd_review", "results": "result_review"}
PATTERN = re.compile(
    r"\b(?P<label>rubric(?:\s+(?:score|total|totals|records))?"
    r"(?:\s+(?:is|remains))?|code(?:\s+overall)?|"
    r"cfd(?:\s+overall)?|results(?:\s+overall)?)\s*[:=]?\s*(?:\*\*)?"
    r"(?P<number>\d+(?:\.\d+)?)\s*/\s*(?P<denominator>100|5)\b",
    re.IGNORECASE,
)


def compare_report(text: str, scores: dict[str, Any]) -> list[dict[str, Any]]:
    """Compare only unambiguously labeled x/100 or x/5 report statements."""
    findings = []
    for match in PATTERN.finditer(text):
        label = match.group("label").lower().split()[0]
        expected_denominator = "100" if label == "rubric" else "5"
        if match.group("denominator") != expected_denominator:
            continue
        if label == "rubric":
            value = (scores.get("rubric") or {}).get("total_scored")
        else:
            value = ((scores.get("scores") or {}).get(AREAS[label]) or {}).get("overall_score")
        if value is None or isinstance(value, bool):
            continue
        try:
            reported = Decimal(match.group("number"))
            recorded = Decimal(str(value))
        except InvalidOperation:
            continue
        # A reported value is typically rounded to its printed precision.
        precision = len(match.group("number").partition(".")[2])
        tolerance = Decimal("0.5") * (Decimal(10) ** -precision)
        difference = abs(reported - recorded)
        findings.append({
            "criterion": label,
            "line": text.count("\n", 0, match.start()) + 1,
            "reported": match.group("number"),
            "recorded": str(recorded),
            "denominator": int(expected_denominator),
            "difference": str(difference),
            "rounding_tolerance": str(tolerance),
            "disagreement": difference > tolerance,
        })
    return findings


def audit(root: Path) -> dict[str, Any]:
    snapshots = []
    errors = []
    for folder in sorted(root.iterdir()):
        if not folder.is_dir():
            continue
        report = folder / "agent_report.md"
        scorecard = folder / "agent_scores.json"
        if not report.is_file() or not scorecard.is_file():
            errors.append({"snapshot": folder.name, "error": "report or scorecard missing"})
            continue
        try:
            text = report.read_text(encoding="utf-8")
            scores = json.loads(scorecard.read_text(encoding="utf-8"))
            if not isinstance(scores, dict):
                raise ValueError("scorecard is not a JSON object")
            findings = compare_report(text, scores)
        except (OSError, UnicodeError, ValueError, TypeError) as exc:
            errors.append({"snapshot": folder.name, "error": str(exc)})
            continue
        snapshots.append({"snapshot": folder.name, "mentions": findings})
    return {
        "scope": "explicit labeled report totals only; omissions and semantics require manual review",
        "snapshots_checked": len(snapshots),
        "snapshots_with_mentions": sum(bool(x["mentions"]) for x in snapshots),
        "snapshots_with_disagreements": sum(
            any(m["disagreement"] for m in x["mentions"]) for x in snapshots
        ),
        "errors": errors,
        "snapshots": snapshots,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="evaluation outputs directory")
    args = parser.parse_args()
    result = audit(args.root)
    print(json.dumps(result, indent=2))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())

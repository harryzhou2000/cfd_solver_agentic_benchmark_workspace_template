#!/usr/bin/env python3
"""Find case-score notes that may overlook accepted contestant PDF evidence.

This is a prioritization screen, not a score validator. An accepted workspace
PDF may be incomplete or stale; review its provenance, case-specific pages,
submitted data, and physics before changing a score. It does not read or edit
the PDF, contestant workspace, or snapshot artifacts.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1] / "outputs"
ABSENT_COMMITTED = re.compile(r"\b(?:no committed|missing committed|not committed)\b", re.IGNORECASE)


def case_leads(scorecard: dict[str, Any], pdf_record: dict[str, Any]) -> list[dict[str, Any]]:
    """Return low-scored cases with a committed-evidence absence assertion."""
    if pdf_record.get("status") != "accepted" or (pdf_record.get("source") or {}).get("mode") != "workspace_existing":
        return []
    results = []
    for case, item in (scorecard.get("case_scores") or {}).items():
        if not isinstance(item, dict):
            continue
        score = item.get("score")
        notes = item.get("notes") or ""
        if isinstance(score, bool) or not isinstance(score, (int, float)) or not 0 <= score <= 2:
            continue
        if isinstance(notes, str) and ABSENT_COMMITTED.search(notes):
            results.append({"case": case, "score": score, "note": notes})
    return results


def audit(root: Path) -> dict[str, Any]:
    leads = []
    errors = []
    checked = 0
    for folder in sorted(root.iterdir()):
        if not folder.is_dir():
            continue
        try:
            scorecard = json.loads((folder / "agent_scores.json").read_text(encoding="utf-8"))
            record = json.loads((folder / "report_pdf.json").read_text(encoding="utf-8"))
            if not isinstance(scorecard, dict) or not isinstance(record, dict):
                raise ValueError("scorecard or PDF record is not a JSON object")
            cases = case_leads(scorecard, record)
        except (OSError, UnicodeError, ValueError, TypeError, AttributeError) as exc:
            errors.append({"snapshot": folder.name, "error": str(exc)})
            continue
        checked += 1
        if cases:
            leads.append({"snapshot": folder.name,
                          "pdf_sha256": (record.get("snapshot") or {}).get("sha256"),
                          "cases": cases})
    return {
        "scope": "accepted workspace-existing PDF plus low-scored case note asserting missing committed evidence",
        "limitation": "Leads only: verify PDF provenance, digest, readable case plots and numerical evidence; no score is assigned.",
        "snapshots_checked": checked,
        "snapshots_with_leads": len(leads),
        "case_leads": sum(len(entry["cases"]) for entry in leads),
        "errors": errors,
        "leads": leads,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    result = audit(args.root)
    print(json.dumps(result, indent=2))
    return 1 if result["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Vendor an explicitly reviewed main report PDF into an evaluation snapshot."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))
from cfdeval import report_pdf  # noqa: E402


def _submission_commit(snapshot: Path, explicit: str | None) -> str | None:
    try:
        identity = json.loads((snapshot / "run_identity.json").read_text())
    except (OSError, json.JSONDecodeError):
        identity = {}
    value = identity.get("submission_commit")
    recorded = value if isinstance(value, str) and value else None
    if explicit and recorded and explicit != recorded:
        raise ValueError(
            "--submission-commit does not match snapshot run_identity.json")
    return explicit or recorded


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Copy an evaluator-approved main report PDF into a snapshot")
    parser.add_argument("--snapshot", required=True)
    parser.add_argument("--workspace")
    parser.add_argument("--source")
    parser.add_argument("--source-mode",
                        choices=("workspace_existing", "compiled_from_submission"))
    parser.add_argument("--source-tex")
    parser.add_argument("--submission-commit")
    parser.add_argument("--build-command")
    parser.add_argument("--evaluator", required=True)
    parser.add_argument("--notes")
    parser.add_argument("--approved", action="store_true")
    parser.add_argument("--visually-reviewed", action="store_true")
    parser.add_argument("--main-report-confirmed", action="store_true")
    parser.add_argument("--readable", action="store_true")
    parser.add_argument("--absent-reason")
    args = parser.parse_args(argv)

    snapshot = Path(args.snapshot)
    try:
        submission = _submission_commit(snapshot, args.submission_commit)
        if args.absent_reason is not None:
            if any((args.source, args.source_mode, args.source_tex, args.approved,
                    args.visually_reviewed, args.main_report_confirmed,
                    args.readable, args.build_command)):
                parser.error("--absent-reason cannot be combined with PDF source options")
            record = report_pdf.record_absent(
                snapshot=snapshot, evaluator=args.evaluator,
                reason=args.absent_reason, submission_commit=submission)
        else:
            if not args.approved:
                parser.error("accepted PDF capture requires --approved after visual review")
            missing = [name for name, value in (
                ("--workspace", args.workspace), ("--source", args.source),
                ("--source-mode", args.source_mode), ("--source-tex", args.source_tex),
                ("--notes", args.notes), ("submission commit", submission),
            ) if not value]
            if missing:
                parser.error("accepted PDF capture requires " + ", ".join(missing))
            record = report_pdf.record_accepted(
                snapshot=snapshot, workspace=Path(args.workspace),
                source=Path(args.source), source_mode=args.source_mode,
                source_tex=args.source_tex, submission_commit=submission,
                evaluator=args.evaluator, notes=args.notes,
                build_command=args.build_command,
                visually_reviewed=args.visually_reviewed,
                main_report_confirmed=args.main_report_confirmed,
                readable=args.readable)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(record, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

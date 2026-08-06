"""cfdeval CLI: extract / summarize / check / query entry point."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        print("usage: cfdeval <check|query|summarize|extract-metadata|"
              "extract-expenses|extract-measurements|extract-configs|"
              "extract-sessions|review-forms|gui> [...]")
        return 0 if argv else 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "check":
        from cfdeval import validation
        return validation.check_cli(rest)
    if cmd == "query":
        from cfdeval import query
        return query.query_cli(rest)
    if cmd == "extract-metadata":
        from cfdeval import metadata
        return metadata.main(rest)
    if cmd == "extract-expenses":
        from cfdeval import expenses
        return expenses.main(rest)
    if cmd == "extract-measurements":
        from cfdeval import measurements
        return measurements.main(rest)
    if cmd == "extract-configs":
        from cfdeval import configs
        return configs.main(rest)
    if cmd == "extract-sessions":
        from cfdeval import sessions
        return sessions.main(rest)
    if cmd == "gui":
        tool = Path(__file__).resolve().parents[2] / "gui" / "server.py"
        return subprocess.run([sys.executable, str(tool), *rest]).returncode
    if cmd == "review-forms":
        tool = Path(__file__).resolve().parents[2] / "tools" / "generate_review_forms.py"
        return subprocess.run([sys.executable, str(tool), *rest]).returncode
    if cmd == "summarize":
        tool = Path(__file__).resolve().parents[2] / "tools" / "summarize.py"
        return subprocess.run([sys.executable, str(tool), *rest]).returncode
    print(f"unknown command: {cmd}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())

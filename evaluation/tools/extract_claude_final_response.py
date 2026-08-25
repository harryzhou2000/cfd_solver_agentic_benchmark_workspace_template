#!/usr/bin/env python3
"""Extract the terminal Claude Code response from a confirmed root session."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from cfdeval import claude_data  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--root", required=True,
                        help="operator-confirmed terminal Claude root session id")
    parser.add_argument("--claude-root", default=None)
    parser.add_argument("--out", required=True,
                        help="contestant_final_response.md output path")
    parser.add_argument("--metadata-out", default=None,
                        help="optional JSON provenance output path")
    args = parser.parse_args(argv)
    try:
        result = claude_data.final_response(
            args.workspace, args.root, args.claude_root)
    except ValueError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    metadata = {key: value for key, value in result.items() if key != "text"}
    if args.metadata_out:
        path = Path(args.metadata_out)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(metadata, indent=2) + "\n")
    if result["status"] != "complete":
        print(f"ERROR: final response is {result['status']}: "
              f"{'; '.join(result.get('limitations', []))}", file=sys.stderr)
        return 2
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(result["text"], encoding="utf-8")
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

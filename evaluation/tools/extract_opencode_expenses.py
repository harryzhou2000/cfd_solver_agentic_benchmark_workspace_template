#!/usr/bin/env python3
"""Write selected-tree OpenCode expenses from an extracted metadata sidecar."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from cfdeval.expenses import opencode_expense_facts  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--metadata", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--roots", default=None,
                        help="comma-separated manually confirmed root session ids")
    parser.add_argument(
        "--cost-metadata", default=str(root / "config" / "cost_metadata.json"))
    args = parser.parse_args(argv)

    metadata = json.loads(Path(args.metadata).read_text())
    if (metadata.get("harness") or {}).get("harness") != "opencode":
        parser.error("metadata harness must be opencode")
    roots = [root.strip() for root in (args.roots or "").split(",") if root.strip()]
    try:
        facts = opencode_expense_facts(
            metadata, str(Path(args.workspace).resolve()), args.cost_metadata,
            roots or None)
    except ValueError as exc:
        parser.error(str(exc))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(facts, indent=2) + "\n")
    print(f"wrote {out}")
    print(f"tokens={facts['tokens']['total']:,} "
          f"cost≈${facts['cost_estimate_usd']['total']:.4f} "
          f"provider=${facts['cost_estimate_usd']['provider_reported_total']:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

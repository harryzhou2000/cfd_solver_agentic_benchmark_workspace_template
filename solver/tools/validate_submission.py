#!/usr/bin/env python3
"""Validate final output directories with the benchmark validator and sanity file."""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK = ROOT.parent / "cfd_solver_agentic_benchmark"
CASE_DIR = BENCHMARK / "inputs" / "cases"
EXPECTED_CASE_IDS = {path.stem for path in CASE_DIR.glob("*.json")}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, nargs="?", default=ROOT / "results")
    parser.add_argument("--report", type=Path, default=ROOT / "report")
    args = parser.parse_args()
    # Audit/probe directories are deliberately preserved in the workspace;
    # only the supplied benchmark case IDs are final submission packages.
    case_dirs = sorted(path for path in args.results.iterdir()
                       if path.is_dir() and path.name in EXPECTED_CASE_IDS)
    if not case_dirs:
        parser.error(f"no output directories in {args.results}")
    command = [sys.executable, str(BENCHMARK / "examiner" / "validate_outputs.py"), *(str(path) for path in case_dirs), "--report", str(args.report)]
    checked = subprocess.run(command).returncode
    sanity_path = args.report / "sanity_checks.json"
    if not sanity_path.exists():
        print(f"missing {sanity_path}", file=sys.stderr)
        return 1
    data = json.loads(sanity_path.read_text())
    failed = [item.get("case_id", "unknown") for item in data.get("cases", []) if not item.get("checks", {}).get("passed")]
    if failed:
        print("sanity checks failed: " + ", ".join(failed), file=sys.stderr)
        return 1
    return checked


if __name__ == "__main__":
    raise SystemExit(main())

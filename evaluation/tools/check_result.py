#!/usr/bin/env python3
"""Thin wrapper around cfdeval format-checking.

Usage: python3 evaluation/tools/check_result.py <result-folder>
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from cfdeval.validation import check_cli  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(check_cli())

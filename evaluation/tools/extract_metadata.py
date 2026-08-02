#!/usr/bin/env python3
"""Thin wrapper around the centralized cfdeval.metadata module.

Usage: python3 evaluation/tools/extract_metadata.py --workspace <dir> [...]
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from cfdeval.metadata import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())

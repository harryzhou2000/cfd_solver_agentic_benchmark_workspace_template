#!/usr/bin/env python
"""Extract candidate numeric literals from the LaTeX snapshot.

Reads the frozen snapshot in scratch/hardcoded_sweep/snapshot/ and prints
file:line:literal plus the full source line, so the literal can be judged in
context.  Filters nothing aggressively -- judgement is the reader's job -- but
does mark lines that look like macro definitions or comments.
"""
import re
import sys
from pathlib import Path

SNAP = Path(__file__).resolve().parent / "snapshot"

NUM = re.compile(r"(?<![A-Za-z0-9_.])(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)")


def main() -> int:
    targets = sys.argv[1:]
    if not targets:
        targets = sorted(p.name for p in SNAP.glob("*.tex"))
    for name in targets:
        path = SNAP / name
        for lineno, raw in enumerate(path.read_text().splitlines(), 1):
            line = raw.rstrip()
            stripped = line.lstrip()
            if stripped.startswith("%"):
                continue
            hits = NUM.findall(line)
            if not hits:
                continue
            kind = "DEF" if stripped.startswith((r"\def", r"\newcommand")) else "TXT"
            print("%s:%d:%s:%s" % (name, lineno, kind, ",".join(hits)))
            print("    | %s" % line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

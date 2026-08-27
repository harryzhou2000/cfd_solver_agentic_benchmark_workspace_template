"""Geometric extrapolation of a decaying force tail, with an honest error bar.

If the sub-block means of C_D decay geometrically towards an asymptote, the ratio
of successive decrements is roughly constant and the remaining distance is the sum
of the tail of a geometric series.  This script fits that ratio over the trailing
window and reports the implied asymptote, together with the spread obtained from
the range of observed ratios, so the extrapolation is quoted as a bracket rather
than as a single unjustified number.

    ../.venv/bin/python extrapolate_tail.py <case_dir> [column]
"""

import csv
import sys

WINDOW = 500
BLOCKS = 10


def blocks(path, col):
    rows = list(csv.DictReader(open(path)))
    clean = []
    for r in rows:
        if clean and int(clean[-1]["step"]) == int(r["step"]):
            continue
        clean.append(r)
    v = [float(r[col]) for r in clean[-WINDOW:]]
    n = WINDOW // BLOCKS
    return [sum(v[i * n:(i + 1) * n]) / n for i in range(BLOCKS)], v[-1]


def main(case_dir, col="cd"):
    b, final = blocks(case_dir + "/forces.csv", col)
    d = [b[i + 1] - b[i] for i in range(len(b) - 1)]
    if any(x == 0 for x in d[:-1]):
        print("zero decrement; not extrapolable")
        return
    ratios = [d[i + 1] / d[i] for i in range(len(d) - 1)]
    print("%s [%s]" % (case_dir.rstrip("/").split("/")[-1], col))
    print("  block means   " + " ".join("%.6f" % x for x in b))
    print("  decrements    " + " ".join("%+.2e" % x for x in d))
    print("  ratios        " + " ".join("%.3f" % x for x in ratios))
    use = [r for r in ratios[-4:] if 0.0 < r < 1.0]
    if not use:
        print("  decay not geometric in (0,1); no defensible extrapolation")
        return
    lo, hi = min(use), max(use)
    for tag, r in (("mean ratio", sum(use) / len(use)), ("lo", lo), ("hi", hi)):
        rem = d[-1] * r / (1.0 - r)
        print("  %-11s r=%.3f  remaining %+.2e  asymptote %.6f"
              % (tag, r, rem, b[-1] + rem))
    print("  final row value %.6f" % final)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "cd")

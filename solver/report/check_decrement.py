"""Decrement decay ratio for a steady run, swept over window and sub-block count.

The ratio classifies a monotone trailing window as an asymptotic approach (ratio
below 1, tail sum finite) or an unfinished march (ratio at or above 1).  Because
the ratio is estimated from sub-block means, it is worth confirming that a verdict
is not an artifact of the particular window and block count chosen -- so this
sweeps both and prints the amplification factor r/(1-r) that decides whether an
extrapolated asymptote may be quoted at all.

    ../.venv/bin/python check_decrement.py <case_dir> [column]
"""

import csv
import sys


def series(path, col):
    rows = list(csv.DictReader(open(path)))
    clean = []
    for r in rows:
        if any(r.get(k) in (None, "") for k in ("step", col)):
            continue
        if clean and int(clean[-1]["step"]) == int(r["step"]):
            continue  # duplicated final row: keep the in-loop value
        clean.append(r)
    return [float(r[col]) for r in clean]


def ratio(v, window, blocks):
    w = v[-window:]
    n = window // blocks
    b = [sum(w[i * n:(i + 1) * n]) / n for i in range(blocks)]
    d = [b[i + 1] - b[i] for i in range(blocks - 1)]
    rs = [d[i + 1] / d[i] for i in range(len(d) - 1) if d[i] != 0.0]
    return (sum(rs) / len(rs) if rs else None), d


def main(case_dir, col="cd"):
    v = series(case_dir + "/forces.csv", col)
    print("%s [%s], %d rows" % (case_dir.rstrip("/").split("/")[-1], col, len(v)))
    print("%-8s %-8s %10s %14s" % ("window", "blocks", "ratio", "amplification"))
    for window in (500, 1000, 2000):
        for blocks in (10, 9, 5):
            if window > len(v):
                continue
            r, _ = ratio(v, window, blocks)
            if r is None:
                continue
            amp = r / (1.0 - r) if r < 1.0 else float("inf")
            print("%-8d %-8d %10.4f %14.1f" % (window, blocks, r, amp))
    _, d = ratio(v, 500, 10)
    print()
    print("decrements (window 500, 10 blocks):")
    print("  " + " ".join("%+.2e" % x for x in d))
    print("  all positive: %s   all negative: %s"
          % (all(x > 0 for x in d), all(x < 0 for x in d)))


if __name__ == "__main__":
    main(sys.argv[1].rstrip("/"), sys.argv[2] if len(sys.argv) > 2 else "cd")

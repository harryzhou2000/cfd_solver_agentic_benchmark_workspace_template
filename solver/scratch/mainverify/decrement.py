#!/usr/bin/env python3
"""Re-derive the decrement-ratio discriminator for the M2.0 laminar case.

The report quotes ratios 1.11,1.27,0.86,0.85,1.04,1.03,1.00,0.99 (mean 1.019) and
concludes the tail does not decay.  The solver's own note now says 0.919.  Work it
out from forces.csv directly, and test the sensitivity to the window and the
number of sub-blocks so the answer cannot be a windowing artifact.  Read-only.
"""
import csv
from pathlib import Path


def load(case):
    steps, cd = [], []
    with (Path("results") / case / "forces.csv").open() as fh:
        for row in csv.DictReader(fh):
            try:
                s = int(float(row["step"])); c = float(row["cd"])
            except (ValueError, TypeError):
                continue
            steps.append(s); cd.append(c)
    # drop the re-emitted best-state row so the march is strictly increasing
    march = []
    for s, c in zip(steps, cd):
        if march and s <= march[-1][0]:
            continue
        march.append((s, c))
    return march


def ratios(cd, nblocks):
    """Mean of successive |decrement| ratios over nblocks sub-block means."""
    n = len(cd) // nblocks
    if n < 2:
        return None, []
    means = [sum(cd[i * n:(i + 1) * n]) / n for i in range(nblocks)]
    dec = [means[i + 1] - means[i] for i in range(len(means) - 1)]
    rs = []
    for i in range(len(dec) - 1):
        if abs(dec[i]) > 0.0:
            rs.append(abs(dec[i + 1]) / abs(dec[i]))
    return (sum(rs) / len(rs) if rs else None), dec


for case in ("naca0012_m200_laminar_re5000", "naca0012_m080_laminar_re5000"):
    march = load(case)
    cd = [c for _, c in march]
    print(f"== {case}: {len(march)} force rows, steps {march[0][0]}..{march[-1][0]}")
    print(f"   final C_D {cd[-1]:.6f}")
    for window in (500, 1000, 2000):
        seg = cd[-window:]
        for nb in (10, 9, 5):
            r, dec = ratios(seg, nb)
            if r is None:
                continue
            signs = "".join("+" if d > 0 else "-" for d in dec)
            print(f"   window {window:5d}  blocks {nb:2d}  mean ratio {r:.4f}  "
                  f"decrement signs {signs}")
    print()

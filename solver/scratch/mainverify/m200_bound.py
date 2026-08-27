#!/usr/bin/env python3
"""Where do the 9 over-bound faces sit, and is the violation localised?

If the faces exceeding the post-shock pitot ceiling are clustered at the
leading edge in the smallest cells, the violation is the documented
limiter/shock-position limitation.  If they are scattered over the body it would
indicate something worse.  Read-only.
"""
import csv
from pathlib import Path

G = 1.4


def pitot_cp(m):
    m2 = m * m
    a = ((G + 1.0) ** 2 * m2 / (4.0 * G * m2 - 2.0 * (G - 1.0))) ** (G / (G - 1.0))
    b = (1.0 - G + 2.0 * G * m2) / (G + 1.0)
    return (a * b - 1.0) / (0.5 * G * m2)


BOUND = pitot_cp(2.0)
print(f"post-shock pitot ceiling at M=2.0: Cp = {BOUND:.6f}\n")

for case in ("naca0012_m200_inviscid", "naca0012_m200_laminar_re5000"):
    rows = list(csv.DictReader((Path("results") / case / "surface.csv").open()))
    over = [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in rows
            if float(r["cp"]) > BOUND]
    xs = [float(r["x"]) for r in rows]
    print(f"== {case}: {len(over)} of {len(rows)} faces over the bound")
    if over:
        print(f"   x range of violating faces: {min(o[0] for o in over):.6f} .. "
              f"{max(o[0] for o in over):.6f}   (chord spans {min(xs):.4f}..{max(xs):.4f})")
        print("   the violating faces:")
        for x, y, cp in sorted(over):
            print(f"     x={x:.6f} y={y:+.6f} cp={cp:.6f}  excess {cp-BOUND:+.6f}")
        frac = max(o[0] for o in over) / max(xs)
        print(f"   all within the first {frac*100:.2f} % of the chord"
              if frac < 0.05 else f"   spread to {frac*100:.1f} % of chord")
    # upper/lower asymmetry at matching |x|
    upper = {round(float(r["x"]), 9): float(r["cp"]) for r in rows if float(r["y"]) > 0}
    lower = {round(float(r["x"]), 9): float(r["cp"]) for r in rows if float(r["y"]) < 0}
    shared = sorted(set(upper) & set(lower))
    if shared:
        worst = max(shared, key=lambda x: abs(upper[x] - lower[x]))
        print(f"   worst upper/lower asymmetry: {abs(upper[worst]-lower[worst]):.6f} "
              f"at x={worst:.6f}  (AoA is 0, so this should be ~0)")
    print()

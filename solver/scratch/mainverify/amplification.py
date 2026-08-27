#!/usr/bin/env python3
"""Geometric-tail amplification for each steady case's terminal drag window.

A monotone tail with per-sub-block decrement ratio r has remaining movement
d*r/(1-r) for a last decrement d, so 1/(1-r) is the amplification applied to the
last observed decrement.  Small r means the extrapolation is well conditioned;
r near 1 means it is not.  Tabulate r, the amplification, the estimated
remaining movement and what that implies for earned digits.  Read-only.
"""
import csv
from pathlib import Path

CASES = [
    "cylinder_m010_laminar_re20",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]


def march(case):
    out = []
    with (Path("results") / case / "forces.csv").open() as fh:
        for row in csv.DictReader(fh):
            try:
                s = int(float(row["step"])); c = float(row["cd"])
            except (ValueError, TypeError):
                continue
            if out and s <= out[-1][0]:
                continue
            out.append((s, c))
    return out


print(f"{'case':32s} {'r':>7s} {'1/(1-r)':>9s} {'last dec':>11s} "
      f"{'remaining':>11s} {'% of Cd':>9s} {'C_D':>10s}")
for case in CASES:
    cd = [c for _, c in march(case)]
    seg = cd[-500:]
    n = len(seg) // 10
    means = [sum(seg[i * n:(i + 1) * n]) / n for i in range(10)]
    dec = [means[i + 1] - means[i] for i in range(9)]
    rs = [abs(dec[i + 1]) / abs(dec[i]) for i in range(8) if abs(dec[i]) > 0]
    r = sum(rs) / len(rs)
    last = dec[-1]
    amp = float("inf") if r >= 1.0 else 1.0 / (1.0 - r)
    rem = abs(last) * r / (1.0 - r) if r < 1.0 else float("inf")
    monotone = all(d > 0 for d in dec) or all(d < 0 for d in dec)
    print(f"{case:32s} {r:7.4f} {amp:9.1f} {last:+11.3e} {rem:11.3e} "
          f"{100*rem/abs(cd[-1]):9.4f} {cd[-1]:10.6f}"
          + ("" if monotone else "   (non-monotone window)"))

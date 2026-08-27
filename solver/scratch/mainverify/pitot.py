#!/usr/bin/env python3
"""Independent check of report_author's M=2.0 objection.

Claim under test: at M=2.0 the isentropic stagnation Cp (2.4373) is the WRONG
ceiling, because the stagnation streamline crosses the bow shock first; the
attainable value is the post-shock (Rayleigh pitot) Cp of 1.6573.
Derive both from scratch and compare against the wall data.  Read-only.
"""
import csv
from pathlib import Path

G = 1.4


def isentropic_cp0(m):
    return (2.0 / (G * m * m)) * ((1.0 + 0.5 * (G - 1.0) * m * m) ** (G / (G - 1.0)) - 1.0)


def rayleigh_pitot_cp(m):
    """Total pressure behind a normal shock, over freestream static, as a Cp.

    p02/p1 = [(g+1)^2 M^2 / (4 g M^2 - 2(g-1))]^(g/(g-1)) * [1 - g + 2 g M^2]/(g+1)
    Cp = (p02 - p1) / (0.5 g M^2 p1)
    """
    m2 = m * m
    a = ((G + 1.0) ** 2 * m2 / (4.0 * G * m2 - 2.0 * (G - 1.0))) ** (G / (G - 1.0))
    b = (1.0 - G + 2.0 * G * m2) / (G + 1.0)
    p02_over_p1 = a * b
    return (p02_over_p1 - 1.0) / (0.5 * G * m2)


for m in (0.15, 0.8, 2.0):
    print(f"M={m}:  isentropic Cp0 = {isentropic_cp0(m):.6f}", end="")
    if m > 1.0:
        print(f"   post-shock pitot Cp = {rayleigh_pitot_cp(m):.6f}"
              f"   (isentropic overstates by {isentropic_cp0(m)/rayleigh_pitot_cp(m)-1.0:+.1%})")
    else:
        print("   (subsonic: no shock, isentropic applies)")

print()
for case, m in (("naca0012_m200_inviscid", 2.0), ("naca0012_m200_laminar_re5000", 2.0)):
    rows = list(csv.DictReader((Path("results") / case / "surface.csv").open()))
    cps = [float(r["cp"]) for r in rows]
    bound = rayleigh_pitot_cp(m)
    over = [c for c in cps if c > bound]
    print(f"{case}: max wall Cp {max(cps):.6f}, pitot bound {bound:.6f}, "
          f"{len(over)} of {len(cps)} faces exceed it")
    print(f"   isentropic Cp0 would be {isentropic_cp0(m):.6f}; "
          f"faces exceeding THAT: {sum(1 for c in cps if c > isentropic_cp0(m))}")

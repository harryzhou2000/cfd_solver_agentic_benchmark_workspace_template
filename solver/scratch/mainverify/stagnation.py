#!/usr/bin/env python3
"""The correct stagnation reference for a compressible flow is not Cp = 1.

For isentropic compressible flow the stagnation pressure coefficient is

    Cp0 = 2/(gamma M^2) * [ (1 + (gamma-1)/2 M^2)^(gamma/(gamma-1)) - 1 ]

which exceeds the incompressible value of 1 and approaches it as M -> 0.
Compare that against what the solver reports at the row nearest the nose.
Read-only.
"""
import csv
import json
from pathlib import Path

GAMMA = 1.4


def stagnation_cp(mach: float, gamma: float = GAMMA) -> float:
    m2 = mach * mach
    return (2.0 / (gamma * m2)) * ((1.0 + 0.5 * (gamma - 1.0) * m2) ** (gamma / (gamma - 1.0)) - 1.0)


for case, mach in (("naca0012_m015_inviscid", 0.15), ("naca0012_m015_laminar_re5000", 0.15)):
    d = Path("results") / case
    rows = list(csv.DictReader((d / "surface.csv").open()))
    rows.sort(key=lambda r: float(r["x"]))
    nose = rows[0]
    cp = float(nose["cp"])
    ref = stagnation_cp(mach)
    print(f"== {case}  Minf={mach}")
    print(f"   isentropic stagnation Cp0        = {ref:.6f}")
    print(f"   incompressible reference          = 1.000000")
    print(f"   solver, row nearest nose (x={float(nose['x']):.2e}) = {cp:.6f}")
    print(f"   vs compressible Cp0: {abs(cp-ref):.2e} absolute, {abs(cp-ref)/ref*100:.4f} %")
    print(f"   vs incompressible 1: {cp-1.0:+.6f}")
    print(f"   max Cp on the wall = {max(float(r['cp']) for r in rows):.6f}")
    print()

for mach in (0.15, 0.8, 2.0):
    print(f"   Cp0({mach}) = {stagnation_cp(mach):.6f}")

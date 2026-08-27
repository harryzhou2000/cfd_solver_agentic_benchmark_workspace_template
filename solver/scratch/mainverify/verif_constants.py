#!/usr/bin/env python3
"""Harvest the solver's own mesh-verification figures from every case log.

The report quotes these as global constants in numbers.tex.  Print the actual
per-case values and the worst case for each, so the report can quote a range or
the worst case rather than one mesh's number.  Read-only.
"""
import re
from pathlib import Path

CASES = [
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]

FIELDS = [
    ("face_closure", r"face-closure error ([0-9.eE+-]+)"),
    ("volume_closure", r"volume-closure error ([0-9.eE+-]+)"),
    ("area_mismatch", r"mismatch ([0-9.eE+-]+)\)"),
    ("lsq_linear_gradient", r"linear-gradient error ([0-9.eE+-]+)"),
    ("conservation_defect", r"conservation defect ([0-9.eE+-]+)"),
    ("uniform_flow", r"uniform-flow residual ([0-9.eE+-]+)"),
]

rows = {}
for case in CASES:
    log = (Path("results") / case / "stdout.log").read_text()
    line = next((l for l in log.splitlines() if "mesh verification:" in l and "face-closure" in l), "")
    vals = {}
    for name, pat in FIELDS:
        m = re.search(pat, line)
        vals[name] = float(m.group(1)) if m else None
    rows[case] = vals

names = [n for n, _ in FIELDS]
print(f"{'case':32s}" + "".join(f"{n[:13]:>15s}" for n in names))
for case, vals in rows.items():
    print(f"{case:32s}" + "".join(
        (f"{vals[n]:15.3e}" if vals[n] is not None else f"{'-':>15s}") for n in names))

print()
print("worst (largest) value across the eight submitted cases:")
for n in names:
    vs = [(v[n], c) for c, v in rows.items() if v[n] is not None]
    if not vs:
        continue
    worst, wcase = max(vs)
    best, bcase = min(vs)
    print(f"  {n:22s} worst {worst:.3e} ({wcase})   best {best:.3e} ({bcase})")

print()
print("distinct meshes: the two cylinder cases share CylinderB1, the six NACA cases share NACA0012_H2,")
print("so each quantity really has only two distinct values -- one per mesh -- except uniform_flow,")
print("which also depends on the case's physics mode.")

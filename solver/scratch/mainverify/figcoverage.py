#!/usr/bin/env python3
"""Visualization coverage matrix: which required figure types does the report display?"""
import csv
import re
from pathlib import Path

REPORT = Path("/workspace/solver/report")

refs = set()
for tex in sorted(REPORT.glob("*.tex")):
    txt = tex.read_text()
    for m in re.finditer(r"cnsfig\{([^}]+)\}", txt):
        refs.add(m.group(1).strip())

listed = {}
for r in csv.DictReader(open(REPORT / "figure_manifest.csv")):
    listed.setdefault(r["case_id"], []).append((r["figure_file"], r["figure_type"], r["variable"]))

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
KINDS = ["residuals", "forces", "cp", "cf", "mach", "pressure", "vorticity", "velocity", "spectrum"]

print("DISPLAYED (via \\cnsfig) per case and figure type")
print("case".ljust(30) + "".join(k[:6].rjust(8) for k in KINDS))
for c in CASES:
    cells = []
    for k in KINDS:
        f = f"{c}_{k}.png"
        cells.append(("show" if f in refs else ("man" if any(f == x[0] for x in listed.get(c, [])) else "-")).rjust(8))
    print(c.ljust(30) + "".join(cells))
print()
print("legend: show = displayed in report; man = in manifest only; - = absent")
print()
extra = sorted(r for r in refs if not any(r.startswith(c) for c in CASES))
print("displayed figures not named after a case:", extra)
print()
print("total displayed:", len(refs))

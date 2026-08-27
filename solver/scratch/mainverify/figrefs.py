#!/usr/bin/env python3
"""Which figures does the report actually display?  Read-only."""
import csv
import re
from pathlib import Path

REPORT = Path("/workspace/solver/report")
FIG = REPORT / "figures"

listed = set(r["figure_file"] for r in csv.DictReader(open(REPORT / "figure_manifest.csv")))
ondisk = set(p.name for p in FIG.glob("*.png"))

refs = set()
for tex in sorted(REPORT.glob("*.tex")):
    txt = tex.read_text()
    for m in re.finditer(r"cnsfig\{([^}]+)\}", txt):
        refs.add(m.group(1).strip())
    for m in re.finditer(r"includegraphics(?:\[[^\]]*\])?\{([^}]+)\}", txt):
        name = m.group(1).split("/")[-1].strip()
        if not name.startswith("#"):
            refs.add(name)

refs = set(r if r.endswith(".png") else r + ".png" for r in refs)

print(f"displayed figures      : {len(refs)}")
print(f"figures on disk        : {len(ondisk)}")
print(f"figures in manifest    : {len(listed)}")
print()
print("DISPLAYED BUT MISSING ON DISK :", sorted(r for r in refs if r not in ondisk) or "none")
print("DISPLAYED BUT NOT IN MANIFEST :", sorted(r for r in refs if r not in listed) or "none")
print()
print(f"in manifest, not displayed ({len(listed - refs)}):")
for f in sorted(listed - refs):
    print("   ", f)
print(f"on disk, not displayed ({len(ondisk - refs)}):")
for f in sorted(ondisk - refs):
    print("   ", f)

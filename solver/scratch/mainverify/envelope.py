#!/usr/bin/env python3
"""Is the residual tail a decaying envelope or a flat limit cycle?

If the envelope of the oscillation still trends down, a longer run reaches the
requested target and the run stopped early.  If the envelope is flat, the target
is unreachable at the supplied controls and the plateau is physical.
Read-only.
"""
import csv
import math
from pathlib import Path

for case, target in (("naca0012_m080_inviscid", 4.0), ("naca0012_m200_inviscid", 3.0)):
    rows = []
    with (Path("results") / case / "residuals.csv").open() as fh:
        for row in csv.DictReader(fh):
            rows.append((int(float(row["step"])), float(row["residual_l2"]), float(row["cfl"])))
    march = []
    for step, res, cfl in rows:
        if march and step <= march[-1][0]:
            break
        march.append((step, res, cfl))
    r0 = march[0][1]
    need = r0 / 10.0 ** target
    print(f"== {case}: target residual {need:.4e}, r0 {r0:.4e}")
    print("   window        min_res     max_res     mean_res    min_cfl  max_cfl")
    n = len(march)
    w = max(50, n // 12)
    for lo in range(0, n - w + 1, w):
        seg = march[lo:lo + w]
        res = [r for _, r, _ in seg]
        cfl = [c for _, _, c in seg]
        print(f"   {seg[0][0]:5d}-{seg[-1][0]:5d}  {min(res):.4e}  {max(res):.4e}  "
              f"{sum(res)/len(res):.4e}  {min(cfl):7.2f} {max(cfl):7.2f}")
    print()

#!/usr/bin/env python3
"""Is the terminal residual of the two short-of-target cases a real plateau?

For each case: report the best residual, where it occurred, how the residual
behaves over the tail, and fit a geometric decay rate to the last decade of
steps to estimate how many more steps the requested target would need.
Read-only.
"""
import csv
import json
import math
from pathlib import Path

CASES = {
    "naca0012_m080_inviscid": 4.0,
    "naca0012_m200_inviscid": 3.0,
    "naca0012_m015_inviscid": 4.0,
    "naca0012_m200_laminar_re5000": 3.0,
}

for case, target in CASES.items():
    d = Path("results") / case
    steps, res = [], []
    with (d / "residuals.csv").open() as fh:
        for row in csv.DictReader(fh):
            steps.append(int(float(row["step"])))
            res.append(float(row["residual_l2"]))
    st = json.loads((d / "run_status.json").read_text())
    r0 = res[0]
    best = min(res)
    ibest = res.index(best)
    orders = math.log10(r0 / best)
    need = r0 / (10.0 ** target)
    print(f"== {case}  target {target} orders")
    print(f"   r0={r0:.6e} best={best:.6e} at step {steps[ibest]} of {steps[-1]}")
    print(f"   orders achieved {orders:.4f}  (run_status says {st['residual_reduction_orders']:.4f})")
    print(f"   residual needed for target: {need:.6e}  shortfall factor {best/need:.3f}")
    # tail behaviour over the last 40% of steps up to the best point
    lo = max(1, int(0.6 * ibest))
    seg = res[lo:ibest + 1]
    if len(seg) > 10:
        rate = (seg[-1] / seg[0]) ** (1.0 / (len(seg) - 1))
        print(f"   tail decay per step over steps {steps[lo]}..{steps[ibest]}: {rate:.8f}")
        if rate < 1.0 and best > need:
            extra = math.log(need / best) / math.log(rate)
            print(f"   at that rate, target would need ~{extra:.0f} more steps "
                  f"(budget remaining {30000 - steps[-1]})")
        elif rate >= 1.0:
            print("   residual is NOT decreasing over the tail: genuine stall")
    # how flat is the very end
    tail = res[max(0, ibest - 200):ibest + 1]
    print(f"   last 200 steps before best: min {min(tail):.4e} max {max(tail):.4e} "
          f"ratio {max(tail)/min(tail):.3f}")
    print()

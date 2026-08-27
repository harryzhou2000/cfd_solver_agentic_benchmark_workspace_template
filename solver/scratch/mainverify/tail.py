#!/usr/bin/env python3
"""What did the residual do AFTER its best point?

The two short-of-target cases restored a best state.  If the march continued
past that point and the residual rose, then a longer run cannot reach the
requested target and the plateau is real.  If it was still falling when the
stationarity test fired, the run stopped early and should be continued.
Read-only.
"""
import csv
import math
from pathlib import Path

for case in ("naca0012_m080_inviscid", "naca0012_m200_inviscid"):
    rows = []
    with (Path("results") / case / "residuals.csv").open() as fh:
        for row in csv.DictReader(fh):
            rows.append((int(float(row["step"])), float(row["residual_l2"])))
    # The final row is the restored best state re-emitted; the march itself is
    # the strictly increasing-step prefix.
    march = []
    for step, res in rows:
        if march and step <= march[-1][0]:
            break
        march.append((step, res))
    steps = [s for s, _ in march]
    res = [r for _, r in march]
    r0 = res[0]
    best = min(res)
    ib = res.index(best)
    print(f"== {case}")
    print(f"   march covers steps {steps[0]}..{steps[-1]} ({len(march)} rows)")
    print(f"   best residual {best:.6e} at step {steps[ib]}")
    print(f"   residual at last marched step {steps[-1]}: {res[-1]:.6e} "
          f"(factor {res[-1]/best:.3f} above best)")
    after = res[ib:]
    print(f"   after the best point: {len(after)-1} more steps, "
          f"max {max(after):.4e} (factor {max(after)/best:.2f} above best), "
          f"min {min(after):.4e}")
    rose = sum(1 for i in range(ib, len(res) - 1) if res[i + 1] > res[i])
    print(f"   of the {len(res)-1-ib} steps after the best point, "
          f"{rose} increased the residual")
    # local decay rate over the last 300 steps before the best point
    for win in (100, 300, 500):
        lo = max(0, ib - win)
        if ib - lo > 5:
            rate = (res[ib] / res[lo]) ** (1.0 / (ib - lo))
            print(f"   local rate over the {ib-lo} steps before best: {rate:.8f}", end="")
            if rate < 1.0:
                target = 4.0 if "m080" in case else 3.0
                need = r0 / 10.0 ** target
                extra = math.log(need / best) / math.log(rate)
                print(f"  -> {extra:.0f} more steps to target")
            else:
                print("  -> not decaying")
    print()

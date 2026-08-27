"""Apply the sensitivity study's OWN ordering + metric definitions to the current
production naca0012_m015_inviscid surface.csv, so the report's "production run
(1.728)" figure is compared like with like.

Imports analyze_stagnation.py read-only; does not write anything there.
"""
import sys

sys.path.insert(0, "/workspace/solver/scratch/sensitivity")

import numpy as np  # noqa: E402
import analyze_stagnation as A  # noqa: E402

PROD = "/workspace/solver/results/naca0012_m015_inviscid"
VARIANTS = {
    "naca_floor005": "/workspace/solver/scratch/sensitivity/runs/naca_floor005",
    "naca_floor00": "/workspace/solver/scratch/sensitivity/runs/naca_floor00",
}


def whole_and_trimmed(path):
    cols = A.read_surface(path + "/surface.csv")
    tag = cols.get("tag")
    x, y, cp = cols["x"], cols["y"], cols["cp"]
    if tag is not None:
        keep_wall = np.array([str(t) in ("WALL", "bc-4") for t in tag])
        x, y, cp = x[keep_wall], y[keep_wall], cp[keep_wall]
    order = A.signed_arclength_order(x, y)
    xo, cpo = x[order], cp[order]
    whole = A.open_chain_metrics(cpo)
    keep = xo < 0.99
    trimmed = A.open_chain_metrics(cpo[keep])
    return whole, trimmed, len(cpo), int(keep.sum())


for name, path in [("PRODUCTION naca0012_m015_inviscid", PROD)] + list(VARIANTS.items()):
    whole, trimmed, n, nk = whole_and_trimmed(path)
    print("%s" % name)
    print("   n=%d  whole-surface oscillation_ratio = %.6f" % (n, whole["oscillation_ratio"]))
    print("   max|d1| = %.6f   max|d2| = %.6f"
          % (whole["max_abs_first_difference"], whole["max_abs_second_difference"]))
    print("   excl. final 1%% chord: n=%d  oscillation_ratio = %.6f"
          % (nk, trimmed["oscillation_ratio"]))
    print()

print("report claims: whole-surface 1.72 (variants), production 1.728,")
print("               falls to 0.263 excluding final 1%% of chord,")
print("               Cp jump at the blunt TE about 1.46")

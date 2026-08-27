"""Second pass on sec_sensitivity.tex claims that are not plain table cells:
spreads, percentages, the trailing-edge localisation story, and the
"production run" oscillation ratio of 1.728.
"""
import csv
import json
import os

SENS = "/workspace/solver/scratch/sensitivity"
RESULTS = "/workspace/solver/results"


def surf_cp(path, tag_ok=("WALL", "bc-4")):
    with open(path, newline="") as fh:
        rows = [r for r in csv.DictReader(fh) if r["tag"] in tag_ok]
    return [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in rows]


def osc_ratio(cps):
    """max|second difference| / max|first difference|.

    This is the definition used by scratch/sensitivity/analyze_stagnation.py
    (open_chain_metrics: oscillation_ratio = max_d2 / max_d), so the check
    compares like with like.
    """
    d1 = [abs(cps[i + 1] - cps[i]) for i in range(len(cps) - 1)]
    d2 = [abs(cps[i + 2] - 2 * cps[i + 1] + cps[i]) for i in range(len(cps) - 2)]
    if not d1 or not d2:
        return None
    return max(d2) / max(d1)


# --- floor sweep spread -----------------------------------------------------
cds = {"0.05": 2.018119, "0.01": 2.017701, "0.00": 2.017754}
spread = max(cds.values()) - min(cds.values())
print("floor sweep: printed spread 4.2e-4 / 0.021 %%")
print("  recomputed spread %.4e   pct of Cd %.4f %%"
      % (spread, 100.0 * spread / cds["0.05"]))

# --- order-1 vs order-2 -----------------------------------------------------
cd1, cd2 = 3.239634, 2.018119
pd1, pd2 = 2.323919, 1.220943
print()
print("order sens: printed overpredict 60.5 %%, pressure rises ~90 %%")
print("  cd  %.6f vs %.6f  -> %+.2f %%" % (cd1, cd2, 100.0 * (cd1 / cd2 - 1.0)))
print("  pdrag %.6f vs %.6f -> %+.2f %%" % (pd1, pd2, 100.0 * (pd1 / pd2 - 1.0)))

# --- venkat spread ----------------------------------------------------------
v1, v5, v10 = 2.032244, 2.018119, 2.015887
print()
print("venkat: printed total spread 0.81 %%, monotone")
print("  spread %.6e  pct(vs K=1) %.4f %%  pct(vs K=5) %.4f %%"
      % (v1 - v10, 100.0 * (v1 - v10) / v1, 100.0 * (v1 - v10) / v5))
print("  monotone decreasing in K: %s" % (v1 > v5 > v10))

# --- trailing-edge localisation on the production naca run ------------------
print()
prod = os.path.join(RESULTS, "naca0012_m015_inviscid", "surface.csv")
pts = surf_cp(prod)
cps = [c for _, _, c in pts]
print("production naca0012_m015_inviscid surface.csv (n=%d)" % len(cps))
print("  whole-surface oscillation ratio %.6f   (report says 1.728)"
      % osc_ratio(cps))
xs = [x for x, _, _ in pts]
print("  max x on wall %.6f  (report cites a blunt TE at x/c=1.005)" % max(xs))
keep = [c for x, _, c in pts if x < 0.99]
print("  excluding final 1%% of chord: n=%d ratio %.6f  (report says 0.263)"
      % (len(keep), osc_ratio(keep)))

# largest cp jump, and where
jumps = [(abs(cps[i + 1] - cps[i]), pts[i][0], pts[i + 1][0])
         for i in range(len(cps) - 1)]
jumps.sort(reverse=True)
print("  largest |dCp| %.4f between x=%.5f and x=%.5f  (report says about 1.46)"
      % jumps[0])

# --- generated-JSON hop ----------------------------------------------------
print()
stag = json.load(open(os.path.join(SENS, "naca_stagnation_metrics.json")))
for name in ("naca_floor005", "naca_floor00"):
    d = stag[name]
    le = d["surface"]["leading_edge"]
    rh = d["residual_health"]
    print("%s: LE alternations %s of %s, osc_ratio %.6f, stag cp %.6f, "
          "sym_max %.4e, max_inner %s, res_inc_frac %.5f, cfl_rej(from note)"
          % (name, le["sign_alternations"], le["n"], le["oscillation_ratio"],
             d["surface"]["stagnation_face"]["cp"], le["symmetry_max_asymmetry"],
             rh["max_inner_iterations_used"], rh["residual_increase_fraction"]))
    print("    whole-surface osc ratio %.6f" % d["surface"]["whole_surface"]["oscillation_ratio"])
    print("    note: %s" % d["run_status"]["notes"])

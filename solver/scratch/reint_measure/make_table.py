"""Emit the markdown comparison table from reintegrate.json."""
import json

import numpy as np

R = json.load(open("/workspace/solver/scratch/reint_measure/reintegrate.json"))


def rel(a, b):
    return (a - b) / abs(b) if b else None


def fr(v):
    return "exactly 0" if v is None else "%+.2e" % v


print("### Pressure drag\n")
print("| case | re-integrated (lsq) | forces.csv | abs diff | rel diff | crude-L rel diff |")
print("|---|---|---|---|---|---|")
for r in R:
    print("| %s | %.12f | %.12f | %+.3e | %+.2e | %+.2e |" % (
        r["case"], r["pd_lsq"], r["pd_solver"], r["pd_lsq"] - r["pd_solver"],
        rel(r["pd_lsq"], r["pd_solver"]), rel(r["pd_crude"], r["pd_solver"])))

print("\n### Viscous drag\n")
print("| case | re-integrated (lsq) | forces.csv | abs diff | rel diff | crude-L rel diff |")
print("|---|---|---|---|---|---|")
for r in R:
    if r["inviscid"]:
        print("| %s | 0.000000000000 | 0.000000000000 | 0.000e+00 | exactly 0 | exactly 0 |" % r["case"])
    else:
        print("| %s | %.12f | %.12f | %+.3e | %+.2e | %+.2e |" % (
            r["case"], r["vd_lsq"], r["vd_solver"], r["vd_lsq"] - r["vd_solver"],
            rel(r["vd_lsq"], r["vd_solver"]), rel(r["vd_crude"], r["vd_solver"])))

print("\n### Arc-length reconstruction quality\n")
print("| case | lsq residual (max) | perimeter lsq | perimeter crude | perimeter crudeB |")
print("|---|---|---|---|---|")
for r in R:
    print("| %s | %.2e | %.9f | %.9f | %.9f |" % (
        r["case"], r["lsq_resid"], r["perimeter_lsq"],
        r["perimeter_crude"], r["perimeter_crude_b"]))

print("\n### forces.csv final-row structure\n")
print("| case | data rows | last-row step | file max step | structure | final step count | bit-exact dup |")
print("|---|---|---|---|---|---|---|")
for r in R:
    f = r["forces_structure"]
    print("| %s | %d | %d | %d | %s | %d | %s |" % (
        r["case"], f["nrows"], f["last_step"], f["file_max"], f["kind"],
        f["n_occurrences"], f["bit_exact_duplicate"]))

print("\n### Last row vs first occurrence of final step (pressure drag)\n")
print("| case | my lsq value | LAST row | rel to LAST | FIRST occurrence | rel to FIRST |")
print("|---|---|---|---|---|---|")
for r in R:
    pf = r["pd_solver_first_occurrence"]
    print("| %s | %.12f | %.12f | %+.2e | %.12f | %+.2e |" % (
        r["case"], r["pd_lsq"], r["pd_solver"], rel(r["pd_lsq"], r["pd_solver"]),
        pf, rel(r["pd_lsq"], pf)))

cyl = [r for r in R if "cylinder" in r["case"]][0]
print("\n### Cylinder analytic validation\n")
print("exact chord of %d-gon inscribed in r=0.5: %.15f" % (cyl["nfaces"], cyl["exact_chord"]))
print("lsq max |L - chord|   = %.3e" % cyl["lsq_vs_exact_chord_maxerr"])
print("crude max |L - chord| = %.3e" % cyl["crude_vs_exact_chord_maxerr"])
print("perimeter (lsq)   = %.12f" % cyl["perimeter_lsq"])
print("perimeter (exact 100-gon) = %.12f" % cyl["perimeter_exact"])
print("pi                = %.12f" % np.pi)
print("polygon deficit pi - P_100gon = %.3e (relative %.3e)" % (
    np.pi - cyl["perimeter_exact"], (np.pi - cyl["perimeter_exact"]) / np.pi))
print("lsq perimeter vs exact 100-gon: %.3e" % (cyl["perimeter_lsq"] - cyl["perimeter_exact"]))

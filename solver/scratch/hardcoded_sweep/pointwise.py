#!/usr/bin/env python
"""Independent re-derivation of the pointwise surface checks.

Deliberately does NOT import report/check_m200_physics.py: the point is to
re-derive the numbers from surface.csv with independently written arithmetic so
agreement is evidence rather than a restatement.
"""
import csv
import math
import sys

RESULTS = "/workspace/solver/results"
GAMMA = 1.4


def read_surface(case):
    with open("%s/%s/surface.csv" % (RESULTS, case), newline="") as fh:
        return list(csv.DictReader(fh))


def wall(case):
    rows = read_surface(case)
    keep = [r for r in rows if r["tag"] in ("WALL", "bc-4")]
    return [
        {
            "x": float(r["x"]), "y": float(r["y"]),
            "cp": float(r["cp"]), "cf": float(r["cf"]),
            "mach": float(r["mach"]), "u": float(r["u"]), "v": float(r["v"]),
            "p": float(r["pressure"]), "rho": float(r["rho"]),
        }
        for r in keep
    ]


def pitot_cp(minf, gamma=GAMMA):
    """Rayleigh pitot: total pressure behind a normal shock over freestream static."""
    if minf <= 1.0:
        #: no shock below M=1, so the isentropic stagnation value is the ceiling
        return isentropic_cp(minf, gamma)
    m2 = minf * minf
    g = gamma
    a = ((g + 1.0) ** 2 * m2 / (4.0 * g * m2 - 2.0 * (g - 1.0))) ** (g / (g - 1.0))
    b = (1.0 - g + 2.0 * g * m2) / (g + 1.0)
    return (a * b - 1.0) / (0.5 * g * m2)


def isentropic_cp(minf, gamma=GAMMA):
    m2 = minf * minf
    return ((1.0 + 0.5 * (gamma - 1.0) * m2) ** (gamma / (gamma - 1.0)) - 1.0) / (0.5 * gamma * m2)


def pairs_by_x(rows, tol=1.0e-9):
    up = sorted([(r["x"], r["cp"]) for r in rows if r["y"] > 0.0])
    lo = sorted([(r["x"], r["cp"]) for r in rows if r["y"] < 0.0])
    out = []
    for xu, cu in up:
        best = min(lo, key=lambda t: abs(t[0] - xu))
        if abs(best[0] - xu) < tol:
            out.append((xu, cu, best[1]))
    return out


def nose_row(rows):
    """Row nearest the leading edge (minimum x)."""
    return min(rows, key=lambda r: r["x"])


def report(case, minf):
    rows = wall(case)
    cps = [r["cp"] for r in rows]
    ceil_pitot = pitot_cp(minf)
    ceil_isen = isentropic_cp(minf)
    cmax = max(cps)
    prs = pairs_by_x(rows)
    print("=== %s  (Minf=%s)" % (case, minf))
    print("  wall faces                  %d" % len(rows))
    print("  pitot ceiling Cp            %.6f" % ceil_pitot)
    print("  isentropic Cp0              %.6f" % ceil_isen)
    print("  max surface Cp              %.6f" % cmax)
    print("  min surface Cp              %.6f" % min(cps))
    print("  max Cp rel pitot            %+.4f %%" % (100.0 * (cmax / ceil_pitot - 1.0)))
    print("  faces > pitot               %d" % sum(1 for c in cps if c > ceil_pitot))
    print("  faces > isentropic          %d" % sum(1 for c in cps if c > ceil_isen))
    print("  faces > 1.0                 %d" % sum(1 for c in cps if c > 1.0))
    nose = nose_row(rows)
    print("  nose (min x) x=%.6e cp=%.6f" % (nose["x"], nose["cp"]))
    print("  matched u/l pairs           %d" % len(prs))
    if prs:
        worst = max(prs, key=lambda t: abs(t[1] - t[2]))
        print("  worst pair |dCp|            %.4f at x=%.5f (%.4f vs %.4f)"
              % (abs(worst[1] - worst[2]), worst[0], worst[1], worst[2]))
        print("  mean pair |dCp|             %.6f"
              % (sum(abs(a - b) for _, a, b in prs) / len(prs)))
    print("  max |cf|                    %.6f" % max(abs(r["cf"]) for r in rows))
    print("  max cf                      %.6f" % max(r["cf"] for r in rows))
    print("  min cf                      %.6f" % min(r["cf"] for r in rows))
    print("  max wall mach               %.6f" % max(r["mach"] for r in rows))
    print("  max wall speed              %.6f"
          % max(math.hypot(r["u"], r["v"]) for r in rows))
    print("  max wall pressure           %.6f" % max(r["p"] for r in rows))


CASE_MINF = {
    "naca0012_m015_inviscid": 0.15,
    "naca0012_m080_inviscid": 0.80,
    "naca0012_m200_inviscid": 2.00,
    "naca0012_m015_laminar_re5000": 0.15,
    "naca0012_m080_laminar_re5000": 0.80,
    "naca0012_m200_laminar_re5000": 2.00,
    "cylinder_m010_laminar_re20": 0.10,
}


if __name__ == "__main__":
    want = sys.argv[1:] or list(CASE_MINF)
    for c in want:
        report(c, CASE_MINF[c])

#!/usr/bin/env python3
"""Structural probes: is the appended fallback row a bit-exact duplicate, and
which window convention reproduces the solver's own quoted sub-block ratio."""
RES = "/workspace/solver/results"


def rows_of(case):
    lines = open("%s/%s/forces.csv" % (RES, case)).read().splitlines()
    return [l.strip().split(",") for l in lines[1:] if l.strip()]


print("--- appended final row vs the earlier row at the same step (fallback cases)")
for case, st in [("naca0012_m015_inviscid", 1861),
                 ("naca0012_m080_inviscid", 2546),
                 ("naca0012_m200_inviscid", 4854)]:
    rows = rows_of(case)
    hits = [(i + 2, r) for i, r in enumerate(rows) if int(r[0]) == st]
    print("%s: step %d appears %dx at csv lines %s" % (
        case, st, len(hits), [h[0] for h in hits]))
    a, b = hits[0][1], hits[-1][1]
    print("   entire row text identical: %s" % (a == b))
    print("   cd first=%s last=%s identical=%s" % (a[3], b[3], a[3] == b[3]))

print()
print("--- non-fallback cases: duplicated max step, identical cd or distinct?")
for case in ["cylinder_m010_laminar_re20", "naca0012_m015_laminar_re5000",
             "naca0012_m080_laminar_re5000", "naca0012_m200_laminar_re5000"]:
    rows = rows_of(case)
    mx = max(int(r[0]) for r in rows)
    hits = [r for r in rows if int(r[0]) == mx]
    print("%s: step %d x%d cds=%s identical=%s" % (
        case, mx, len(hits), [h[3] for h in hits],
        len(set(h[3] for h in hits)) == 1))


def r_of(v):
    w = v[-500:]
    m = [sum(w[i * 50:(i + 1) * 50]) / 50 for i in range(10)]
    d = [m[i + 1] - m[i] for i in range(9)]
    return sum(abs(d[i + 1]) / abs(d[i]) for i in range(8)) / 8


print()
print("--- which convention reproduces the solver's quoted 3-dp sub-block ratio")
for case, q in [("cylinder_m010_laminar_re20", 0.736),
                ("naca0012_m200_laminar_re5000", 0.919),
                ("naca0012_m015_laminar_re5000", 0.839)]:
    cd = [float(r[3]) for r in rows_of(case)]
    raw, ded = r_of(cd), r_of(cd[:-1])
    print("%s: quoted %.3f | raw(keeps dup)=%.6f->%.3f %s | drop-dup=%.6f->%.3f %s" % (
        case, q, raw, raw, "MATCH" if round(raw, 3) == q else "no",
        ded, ded, "MATCH" if round(ded, 3) == q else "no"))

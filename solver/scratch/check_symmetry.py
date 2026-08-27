"""Check upper/lower surface Cp symmetry and the pitot Cp bound for a case."""
import csv
import math
import sys

rows = list(csv.DictReader(open(sys.argv[1])))
mach = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
gamma = 1.4

cps = [float(r["cp"]) for r in rows]
print("surface rows: %d" % len(rows))
print("max cp = %.4f   min cp = %.4f" % (max(cps), min(cps)))

# Pitot (total) pressure behind a normal shock, as a Cp ceiling for a steady
# inviscid solution: the highest pressure any point on the body can reach.
m2 = mach * mach
p2_p1 = (2.0 * gamma * m2 - (gamma - 1.0)) / (gamma + 1.0)
m2sq = (2.0 + (gamma - 1.0) * m2) / (2.0 * gamma * m2 - (gamma - 1.0))
pt2_p2 = (1.0 + 0.5 * (gamma - 1.0) * m2sq) ** (gamma / (gamma - 1.0))
pt2_p1 = p2_p1 * pt2_p2
cp_max = (pt2_p1 - 1.0) / (0.5 * gamma * m2)
print("theoretical pitot Cp ceiling at M=%.2f: %.4f" % (mach, cp_max))
over = [c for c in cps if c > cp_max]
print("faces exceeding the ceiling: %d of %d" % (len(over), len(cps)))

# Pair upper and lower surface points by x and compare.
upper = sorted([(float(r["x"]), float(r["cp"])) for r in rows if float(r["y"]) > 0])
lower = sorted([(float(r["x"]), float(r["cp"])) for r in rows if float(r["y"]) < 0])
worst = (0.0, 0.0, 0.0, 0.0)
pairs = 0
for xu, cu in upper:
    cand = min(lower, key=lambda t: abs(t[0] - xu))
    if abs(cand[0] - xu) < 1e-6:
        pairs += 1
        if abs(cu - cand[1]) > worst[0]:
            worst = (abs(cu - cand[1]), xu, cu, cand[1])
print("paired points: %d" % pairs)
print("worst asymmetry %.4f at x=%.5f (upper %+.4f, lower %+.4f)" % worst)

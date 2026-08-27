"""Check the surviving bare literals in the M=2.0 localisation passage:
  - "The remaining 395 faces, covering more than 99.8 % of the surface"
  - "+0.4861 at x=0.000439, against +0.2669 on the nose face itself"
  - "a chord that extends to x=1.0053"
  - "by a factor of 8" (inviscid vs laminar worst pair asymmetry)
"""
import csv

PITOT = 1.6573003216492854  # cross-checked two ways; see report eq:pitot


def wall(case):
    path = "/workspace/solver/results/%s/surface.csv" % case
    with open(path, newline="") as fh:
        rows = [r for r in csv.DictReader(fh) if r["tag"] in ("WALL", "bc-4")]
    return [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in rows]


inv = wall("naca0012_m200_inviscid")
lam = wall("naca0012_m200_laminar_re5000")

over = [(x, y, c) for x, y, c in inv if c > PITOT]
print("inviscid: %d wall faces, %d over pitot" % (len(inv), len(over)))
print("  remaining faces = %d   (report says 395)" % (len(inv) - len(over)))
print("  remaining pct   = %.4f %%  (report says more than 99.8 %%)"
      % (100.0 * (len(inv) - len(over)) / len(inv)))
xs = sorted(x for x, _, _ in over)
print("  over-pitot x range %.6f .. %.6f" % (xs[0], xs[-1]))
print("  max chord x on wall %.6f  (report says 1.0053)" % max(x for x, _, _ in inv))
print("  cluster width as pct of chord %.4f %%  (report says 0.18 %%)"
      % (100.0 * (xs[-1] - xs[0]) / max(x for x, _, _ in inv)))
print("  upper-surface count among the over set: %d"
      % sum(1 for _, y, _ in over if y > 0))

print()
print("  excess above pitot, sorted by x:")
for x, y, c in sorted(over, key=lambda t: t[0]):
    print("    x=%.6f y=%+.6f cp=%.6f excess=%+.4f" % (x, y, c, c - PITOT))

nose = min(inv, key=lambda t: t[0])
print()
print("  nose face x=%.6e cp=%.6f excess=%+.4f  (report says +0.2669)"
      % (nose[0], nose[2], nose[2] - PITOT))
peak = max(over, key=lambda t: t[2])
print("  peak excess %+.4f at x=%.6f  (report says +0.4861 at x=0.000439)"
      % (peak[2] - PITOT, peak[0]))


def worst_pair(rows):
    up = sorted([(x, c) for x, y, c in rows if y > 0.0])
    lo = sorted([(x, c) for x, y, c in rows if y < 0.0])
    best = 0.0
    for xu, cu in up:
        m = min(lo, key=lambda t: abs(t[0] - xu))
        if abs(m[0] - xu) < 1e-9:
            best = max(best, abs(cu - m[1]))
    return best


wi, wl = worst_pair(inv), worst_pair(lam)
print()
print("  worst pair asym inviscid %.4f  laminar %.4f  ratio %.2f  (report says factor 8)"
      % (wi, wl, wi / wl))

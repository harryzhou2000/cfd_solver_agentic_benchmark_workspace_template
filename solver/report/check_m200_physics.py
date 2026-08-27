"""Re-measure the two physical convergence tests for the M=2.0 inviscid airfoil.

Both tests are pointwise and read only the submitted surface.csv, so they are
independent of the solver's own convergence bookkeeping:

  1. Pitot bound.  The highest pressure any point on a body in a steady M=2
     inviscid flow can reach is the total pressure behind a normal shock.  A
     converged inviscid solution cannot exceed the corresponding C_p.
  2. Up/down symmetry.  The section is symmetric at zero incidence, so paired
     upper and lower surface points must carry equal C_p.

    ../.venv/bin/python check_m200_physics.py <case_dir>
"""

import csv
import math
import sys

GAMMA = 1.4
MINF = 2.0


def pitot_cp(minf=MINF, gamma=GAMMA):
    """C_p at the stagnation point behind a normal shock (Rayleigh pitot)."""
    m2sq = (1.0 + 0.5 * (gamma - 1.0) * minf * minf) / (
        gamma * minf * minf - 0.5 * (gamma - 1.0))
    p2p1 = 1.0 + 2.0 * gamma / (gamma + 1.0) * (minf * minf - 1.0)
    # total pressure behind the shock, referred to freestream static
    pt2 = p2p1 * (1.0 + 0.5 * (gamma - 1.0) * m2sq) ** (gamma / (gamma - 1.0))
    return (pt2 - 1.0) / (0.5 * gamma * minf * minf)


def main(case_dir):
    rows = [r for r in csv.DictReader(open(case_dir + "/surface.csv"))]
    cp = [(float(r["x"]), float(r["y"]), float(r["cp"])) for r in rows]
    limit = pitot_cp()
    over = [(x, y, c) for x, y, c in cp if c > limit]
    cmax = max(c for _, _, c in cp)
    print("faces                        %d" % len(cp))
    print("pitot ceiling C_p,max        %.4f" % limit)
    print("observed max C_p             %.4f  (%+.1f %% of ceiling)"
          % (cmax, 100.0 * (cmax / limit - 1.0)))
    print("faces exceeding the ceiling  %d" % len(over))

    # Pair upper and lower surface points by x.
    up = sorted([(x, c) for x, y, c in cp if y > 0.0])
    lo = sorted([(x, c) for x, y, c in cp if y < 0.0])
    pairs = []
    for xu, cu in up:
        best = min(lo, key=lambda t: abs(t[0] - xu))
        if abs(best[0] - xu) < 1.0e-9:
            pairs.append((xu, cu, best[1]))
    if pairs:
        worst = max(pairs, key=lambda t: abs(t[1] - t[2]))
        print("matched upper/lower pairs    %d" % len(pairs))
        print("worst |dCp| between pairs    %.4f at x=%.5f (%.4f vs %.4f)"
              % (abs(worst[1] - worst[2]), worst[0], worst[1], worst[2]))
    else:
        print("no x-matched pairs found")


if __name__ == "__main__":
    main(sys.argv[1].rstrip("/"))

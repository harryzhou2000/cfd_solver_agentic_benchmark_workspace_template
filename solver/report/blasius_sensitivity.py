"""Sensitivity of the Blasius comparison to the extrapolation window.

The report quotes the viscous drag as computed and, separately, an extrapolated
asymptote.  The extrapolation is a geometric-decay model, so its answer depends on
the trailing window and the sub-block count used to estimate the decay ratio.  This
sweeps both so the report can quote a range rather than a single figure that looks
more determined than it is.

    ../.venv/bin/python blasius_sensitivity.py
"""

import csv

CASE = "/workspace/solver/results/naca0012_m015_laminar_re5000/forces.csv"
BLASIUS = 2.0 * 1.328 / (5000.0 ** 0.5)


def in_loop(path, col):
    rows = list(csv.DictReader(open(path)))
    clean = []
    for r in rows:
        if clean and int(clean[-1]["step"]) == int(r["step"]):
            continue
        clean.append(r)
    return [float(r[col]) for r in clean]


def asymptote(v, window, blocks):
    w = v[-window:]
    n = window // blocks
    b = [sum(w[i * n:(i + 1) * n]) / n for i in range(blocks)]
    d = [b[i + 1] - b[i] for i in range(blocks - 1)]
    ratios = [d[i + 1] / d[i] for i in range(len(d) - 1) if d[i] != 0.0]
    use = [x for x in ratios[-4:] if 0.0 < x < 1.0]
    if not use:
        return None, None
    r = sum(use) / len(use)
    return b[-1] + d[-1] * r / (1.0 - r), r


v = in_loop(CASE, "viscous_drag")
print("Blasius 2*1.328/sqrt(5000) = %.6f" % BLASIUS)
print("computed viscous drag      = %.6f  -> %.2f %% below Blasius"
      % (v[-1], 100.0 * (BLASIUS - v[-1]) / BLASIUS))
print()
print("%-8s %-7s %-8s %-10s %s" % ("window", "blocks", "ratio", "asymptote", "vs Blasius"))
for window, blocks in ((500, 10), (1000, 10), (1500, 6), (1500, 10), (2000, 10), (3000, 10)):
    if window > len(v):
        continue
    a, r = asymptote(v, window, blocks)
    if a is None:
        print("%-8d %-7d %s" % (window, blocks, "no geometric decay"))
        continue
    print("%-8d %-7d %-8.3f %-10.6f %.2f %%"
          % (window, blocks, r, a, 100.0 * (BLASIUS - a) / BLASIUS))

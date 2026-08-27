"""Method-compliance checks for the transient, independent of its final state.

These are properties of HOW the run is being integrated rather than of what it
converges to, so they are meaningful while the run is still in progress:

  * the physical time step is exactly the prescribed dt on every row;
  * step numbers increment by exactly one, so there is one force row per PHYSICAL
    step and the history is sampled by physical time rather than by inner iteration;
  * inner-iteration counts lie inside the requested bounds;
  * the inner convergence target is met on every logged step.

Also reports saturation evidence over non-overlapping windows, which is what
justifies the start time chosen for the post-transient statistics.

    ../.venv/bin/python check_transient_method.py
"""

import csv

CASE = "/workspace/solver/results/cylinder_m010_laminar_re200"
DT = 0.01
INNER_LO, INNER_HI = 5, 1000
INNER_TARGET = 1.0e-3


def rows(path, required):
    out = []
    for r in csv.DictReader(open(path)):
        if any(r.get(k) in (None, "") for k in required):
            continue  # partially flushed final row while the solver is writing
        out.append(r)
    return out


f = rows(CASE + "/forces.csv", ("step", "physical_time", "cd", "cl"))
t = [float(r["physical_time"]) for r in f]
s = [int(r["step"]) for r in f]

dts = [t[i + 1] - t[i] for i in range(len(t) - 1)]
worst_dt = max(abs(d - DT) for d in dts) if dts else 0.0
bad_step = sum(1 for i in range(len(s) - 1) if s[i + 1] - s[i] != 1)
print("force rows                  %d  (t = %.2f .. %.2f)" % (len(f), t[0], t[-1]))
print("max |dt - %.2f|              %.3e  over %d intervals" % (DT, worst_dt, len(dts)))
print("step increments != 1        %d" % bad_step)

r = rows(CASE + "/residuals.csv", ("step", "inner_iter"))
it = [int(x["inner_iter"]) for x in r if int(x["inner_iter"]) > 0]
if it:
    print("inner iterations            %d..%d, mean %.1f  (requested %d..%d)"
          % (min(it), max(it), sum(it) / len(it), INNER_LO, INNER_HI))
    print("  inside requested bounds   %s"
          % all(INNER_LO <= v <= INNER_HI for v in it))

ratios = [float(x["inner_res_ratio"]) for x in r
          if x.get("inner_res_ratio") not in (None, "") and float(x["inner_res_ratio"]) > 0]
if ratios:
    met = sum(1 for v in ratios if v <= INNER_TARGET)
    print("inner target %.0e met on     %.2f %% of %d logged steps (worst %.3e)"
          % (INNER_TARGET, 100.0 * met / len(ratios), len(ratios), max(ratios)))

print()
print("%-14s %10s %12s %12s" % ("window", "mean C_D", "C_D p-p", "C_L amp"))
lo = 40.0
while lo + 20.0 <= t[-1] + 1e-9:
    hi = lo + 20.0
    cd = [float(x["cd"]) for x in f if lo <= float(x["physical_time"]) < hi]
    cl = [float(x["cl"]) for x in f if lo <= float(x["physical_time"]) < hi]
    if cd:
        print("t %5.0f-%-5.0f %10.6f %12.6f %12.6f"
              % (lo, hi, sum(cd) / len(cd), max(cd) - min(cd),
                 0.5 * (max(cl) - min(cl))))
    lo = hi

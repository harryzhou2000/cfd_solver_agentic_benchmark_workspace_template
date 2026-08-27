"""Verify whether a steady run ended above its own best residual."""
import csv
import math
import sys

rows = [r for r in csv.DictReader(open(sys.argv[1])) if r.get("residual_l2")]
vals = [(int(float(r["step"])), float(r["residual_l2"]), float(r["cfl"])) for r in rows]
first = vals[0][1]
best = min(vals, key=lambda v: v[1])
last = vals[-1]
print("first  step %6d  res %.4e" % (vals[0][0], first))
print("best   step %6d  res %.4e  cfl %.1f  -> %.2f orders"
      % (best[0], best[1], best[2], math.log10(first / best[1])))
print("final  step %6d  res %.4e  cfl %.1f  -> %.2f orders"
      % (last[0], last[1], last[2], math.log10(first / last[1])))
print("final/best ratio = %.2f  (orders discarded %.2f)"
      % (last[1] / best[1], math.log10(last[1] / best[1])))

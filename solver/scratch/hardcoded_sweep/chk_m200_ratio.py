"""Is the report's claim that naca0012_m200_laminar_re5000 has decrement ratios
averaging 1.019 (i.e. NOT decaying, NOT converged) supported by the current data?

Test it under many reasonable window/blocking conventions so the verdict does not
hinge on one arbitrary choice.
"""
import gt

CASE = "naca0012_m200_laminar_re5000"
PRINTED_RATIOS = [1.11, 1.27, 0.86, 0.85, 1.04, 1.03, 1.00, 0.99]
PRINTED_MEAN = 1.019

rows_all = gt.load_csv(CASE, "forces.csv")[1]
dedup = rows_all[:-1] if rows_all[-1]["step"] == rows_all[-2]["step"] else rows_all

print("case %s" % CASE)
st = gt.load_json(CASE, "run_status.json")
print("  run_status convergence_status : %s" % st["convergence_status"])
print("  run_status orders            : %.4f" % st["residual_reduction_orders"])
print("  run_status final_step        : %s" % st["final_step"])
print("  run_status note              : %s" % st["notes"])
print()
print("  printed ratios %s  mean %.3f" % (PRINTED_RATIOS, PRINTED_MEAN))
print()

for label, rows in (("dedup", dedup), ("raw(with dup)", rows_all)):
    for window in (500, 1000, 2000):
        for nb in (10, 9, 5):
            tail = rows[-window:]
            per = len(tail) // nb
            if per < 2:
                continue
            means = [
                sum(r["cd"] for r in tail[i * per:(i + 1) * per]) / per
                for i in range(nb)
            ]
            dec = [means[i + 1] - means[i] for i in range(nb - 1)]
            rat = [dec[i + 1] / dec[i] for i in range(len(dec) - 1) if dec[i] != 0.0]
            if not rat:
                continue
            rbar = sum(rat) / len(rat)
            allneg = all(d < 0 for d in dec)
            allpos = all(d > 0 for d in dec)
            print("  %-13s w=%-5d nb=%-3d mean_ratio=%.4f  monotone=%s  ratios=%s"
                  % (label, window, nb, rbar,
                     "down" if allneg else ("up" if allpos else "mixed"),
                     ", ".join("%.2f" % v for v in rat)))
print()
print("  decrement magnitudes at w=500 nb=10 (dedup), in 1e-6 units:")
tail = dedup[-500:]
per = 50
means = [sum(r["cd"] for r in tail[i * per:(i + 1) * per]) / per for i in range(10)]
dec = [means[i + 1] - means[i] for i in range(9)]
print("   %s" % ", ".join("%+.4f" % (d * 1e6) for d in dec))
print("  cd over window: first %.9f  last %.9f  span %.3e"
      % (means[0], means[-1], max(means) - min(means)))

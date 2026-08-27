"""Re-derive the sub-block decrement / geometric-tail analysis from forces.csv.

The report's sec:digits argument quotes decrements, ratios, a mean ratio, a
remaining-movement figure, a percentage and an asymptote, and states they are the
SOLVER's own numbers from run_status.json.  So we check both: (a) what the data
gives, (b) what run_status.json currently says.
"""
import re
import gt


def blocks(case, window=500, nblocks=10, drop_dup=True):
    rows = gt.load_csv(case, "forces.csv")[1]
    if drop_dup and len(rows) > 1 and rows[-1]["step"] == rows[-2]["step"]:
        #: final step appears twice (in-loop, then post-loop re-eval)
        rows = rows[:-1]
    tail = rows[-window:]
    per = len(tail) // nblocks
    means = []
    for i in range(nblocks):
        chunk = tail[i * per:(i + 1) * per]
        means.append(sum(r["cd"] for r in chunk) / len(chunk))
    dec = [means[i + 1] - means[i] for i in range(len(means) - 1)]
    ratios = [dec[i + 1] / dec[i] for i in range(len(dec) - 1) if dec[i] != 0.0]
    return rows, tail, means, dec, ratios


for case in ("cylinder_m010_laminar_re20", "naca0012_m200_laminar_re5000"):
    rows, tail, means, dec, ratios = blocks(case)
    print("=== %s" % case)
    print("  rows %d  last step %s  cd_last %.9f"
          % (len(rows), rows[-1]["step"], rows[-1]["cd"]))
    print("  sub-block decrements (1e-3): %s"
          % ", ".join("%.2f" % (d * 1e3) for d in dec))
    print("  ratios: %s" % ", ".join("%.2f" % r for r in ratios))
    if ratios:
        rbar = sum(ratios) / len(ratios)
        print("  mean ratio %.4f" % rbar)
        dlast = dec[-1]
        rem = abs(dlast) * rbar / (1.0 - rbar) if rbar < 1.0 else float("inf")
        print("  d_last %.4e  remaining %.4e" % (dlast, rem))
        cd = rows[-1]["cd"]
        if rem != float("inf"):
            print("  remaining pct %.4f %%   asymptote %.6f"
                  % (100.0 * rem / abs(cd), cd - rem))
        print("  amplification r/(1-r) %.3f"
              % (rbar / (1.0 - rbar) if rbar < 1.0 else float("inf")))
    note = gt.load_json(case, "run_status.json")["notes"]
    print("  --- run_status note ---")
    print("  %s" % note.replace("\n", " "))
    print()

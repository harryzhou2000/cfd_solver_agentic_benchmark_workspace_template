"""Independent check of the force-stationarity window for a completed steady run.

Reads forces.csv, drops the duplicated final row (the post-loop re-evaluation),
and reports the trailing-500-step span, the ten sub-block means, and whether the
trend is monotone.  Used to confirm that a run the solver certified as converged
is not still marching, independently of the solver's own bookkeeping.

    ../.venv/bin/python check_window.py <case_dir> [...]
"""

import csv
import sys

WINDOW = 500
BLOCKS = 10


def load_cd(path):
    rows = list(csv.DictReader(open(path)))
    clean = []
    for r in rows:
        # A row can be short if the file is read while the solver is mid-write;
        # DictReader fills the missing trailing fields with None.  Drop those
        # rather than crashing, so this can be run against a live run.
        if any(r.get(k) in (None, "") for k in ("step", "cd", "pressure_drag", "viscous_drag")):
            continue
        if clean and int(clean[-1]["step"]) == int(r["step"]):
            continue  # duplicated final step: keep the in-loop row
        clean.append(r)
    return clean


def report(case_dir):
    rows = load_cd(case_dir + "/forces.csv")
    if len(rows) < WINDOW:
        print("%-32s only %d rows, window not full" % (case_dir.split("/")[-1], len(rows)))
        return
    w = rows[-WINDOW:]
    cd = [float(r["cd"]) for r in w]
    pd = [float(r["pressure_drag"]) for r in w]
    vd = [float(r["viscous_drag"]) for r in w]
    span = max(cd) - min(cd)
    tol = max(1.0e-2 * max(abs(v) for v in cd), 1.0e-5)
    blk = [sum(cd[i * (WINDOW // BLOCKS):(i + 1) * (WINDOW // BLOCKS)]) / (WINDOW // BLOCKS)
           for i in range(BLOCKS)]
    inc = all(blk[i + 1] > blk[i] for i in range(BLOCKS - 1))
    dec = all(blk[i + 1] < blk[i] for i in range(BLOCKS - 1))
    name = case_dir.rstrip("/").split("/")[-1]
    print("== %s  steps %s..%s" % (name, w[0]["step"], w[-1]["step"]))
    print("   span %.4e  tol %.4e  ratio %.4f  %s"
          % (span, tol, span / tol, "PASS" if span <= tol else "fail"))
    print("   blocks " + " ".join("%.6f" % b for b in blk))
    print("   monotone: %s" % ("increasing" if inc else "decreasing" if dec else "NO (good)"))
    print("   Cd  %.6f -> %.6f   dCd  %+.4e" % (cd[0], cd[-1], cd[-1] - cd[0]))
    print("   Cdp %.6f -> %.6f   d %+.4e (%+.2f %%)"
          % (pd[0], pd[-1], pd[-1] - pd[0], 100.0 * (pd[-1] - pd[0]) / abs(pd[0] or 1)))
    print("   Cdv %.6f -> %.6f   d %+.4e (%+.2f %%)"
          % (vd[0], vd[-1], vd[-1] - vd[0], 100.0 * (vd[-1] - vd[0]) / abs(vd[0] or 1)))


if __name__ == "__main__":
    for d in sys.argv[1:]:
        report(d)

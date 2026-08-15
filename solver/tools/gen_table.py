#!/usr/bin/env python3
"""Emit LaTeX results-table rows from report/run_manifest.csv."""
import csv, os
SOLVER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = os.path.join(SOLVER, "report", "run_manifest.csv")
rows = list(csv.DictReader(open(p)))
def esc(s): return s.replace("_", "\\_")
def fmt(x):
    try:
        v = float(x)
        if abs(v) < 1e-4 and v != 0: return "%.2e" % v
        return "%.4f" % v
    except: return str(x)
for r in rows:
    print(r["case_id"].replace("_","\\_"), "&", r["final_step"], "&",
          fmt(r["final_cd"]), "&", fmt(r["final_cl"]), "&", r["convergence_status"], r"\\")

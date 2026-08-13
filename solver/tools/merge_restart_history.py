#!/usr/bin/env python3
"""Merge residual/force histories of an interrupted run with its resumed
(restarted) run. The pre-restart rows come from the first process (kept in the
result dir before the restart), the post-restart rows from the resumed run.

Usage: merge_restart_history.py <case_dir> <restart_step>
The script expects <case_dir>/residuals.csv and forces.csv to currently hold
the RESUMED run's rows (starting at restart_step), and
<case_dir>/residuals_pre.csv / forces_pre.csv to hold the rows of the original
run. It writes back the merged history."""
import csv
import sys


def load(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def save(path, rows, fieldnames):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)


def merge(pre_path, post_rows, restart_step, name):
    try:
        pre = load(pre_path)
    except FileNotFoundError:
        print(f"  {name}: no pre-restart file, keeping resumed history")
        return
    # keep pre rows with step < restart_step
    keep = [r for r in pre if int(float(r["step"])) < restart_step]
    out = keep + post_rows
    save(pre_path.replace("_pre", ""), out, post_rows[0].keys())
    print(f"  {name}: merged {len(keep)} pre rows + {len(post_rows)} resumed rows")


if __name__ == "__main__":
    case_dir = sys.argv[1]
    restart_step = int(sys.argv[2])
    for name in ("residuals", "forces"):
        merge(f"{case_dir}/{name}_pre.csv",
              load(f"{case_dir}/{name}.csv"), restart_step, name)

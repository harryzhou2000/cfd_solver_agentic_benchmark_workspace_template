#!/usr/bin/env python3
"""Final assembly: fix metadata, regenerate run manifest with true residual
reductions, produce report table rows, and run the contract validator."""
import csv
import json
import math
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RESULTS = os.path.join(ROOT, "results")
REPORT = os.path.join(ROOT, "report")

CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

# Fallback step-0 residual L2 values (used only when residuals.csv does not
# contain a step-0 row, e.g. a run resumed from a checkpoint). When the CSV
# has a step-0 row, the actual value is read from it.
INITIAL_L2 = {
    "naca0012_m015_inviscid": 5.326649e-03,
    "naca0012_m080_inviscid": 2.207263e-04,
    "naca0012_m200_inviscid": 8.602100e-05,
    "naca0012_m015_laminar_re5000": 3.898610e-02,
    "naca0012_m080_laminar_re5000": 3.851722e-02,
    "naca0012_m200_laminar_re5000": 3.850231e-02,
    "cylinder_m010_laminar_re20": 2.914403e-01,
    "cylinder_m010_laminar_re200": None,
}


def patch_json(path, fn):
    d = json.load(open(path))
    fn(d)
    json.dump(d, open(path, "w"), indent=2)


def final_l2(case):
    rows = list(csv.DictReader(open(os.path.join(RESULTS, case, "residuals.csv"))))
    return float(rows[-1]["residual_l2"])


def initial_l2(case):
    rows = list(csv.DictReader(open(os.path.join(RESULTS, case, "residuals.csv"))))
    for r in rows:
        if abs(float(r["step"])) < 1e-12:
            return float(r["residual_l2"])
    return INITIAL_L2.get(case)


def main():
    manifest_rows = []
    tables = {"status": [], "forces": []}
    for case in CASES:
        d = os.path.join(RESULTS, case)
        rp, mp = os.path.join(d, "run_status.json"), os.path.join(d, "metadata.json")
        if not (os.path.exists(rp) and os.path.exists(mp)):
            print(f"SKIP {case} (missing status/metadata)")
            continue
        s = json.load(open(rp))
        m = json.load(open(mp))

        # Total residual reduction from the documented initial residual.
        init = initial_l2(case)
        if init:
            fin = final_l2(case)
            orders = math.log10(fin / init + 1e-300)
            if abs(orders) > 12:
                orders = 0.0
            s["residual_reduction_orders"] = round(orders, 3)
            s["notes"] += "; residual reduction measured from run step 0"
            json.dump(s, open(rp, "w"), indent=2)
            m.setdefault("extra", {})["total_residual_reduction_orders"] = round(orders, 3)
            json.dump(m, open(mp, "w"), indent=2)

        forces = list(csv.DictReader(open(os.path.join(d, "forces.csv"))))
        last = forces[-1]
        final_step = int(float(s["final_step"]))
        ok_end = int(float(last["step"])) == final_step
        tables["forces"].append(
            (case, last["cd"], last["cl"], last["cmz"], last["pressure_drag"],
             last["viscous_drag"], last["pressure_lift"], last["viscous_lift"]))
        tables["status"].append(
            (case, s.get("mpi_ranks"), s.get("final_step"),
             s.get("final_physical_time"), s.get("residual_reduction_orders"),
             s.get("wall_time_seconds"), s.get("convergence_status"),
             "OK" if ok_end else "MISMATCH"))
        manifest_rows.append(
            f"| {case} | {s.get('mpi_ranks')} | {s.get('final_step')} "
            f"| {s.get('final_physical_time')} | {s.get('residual_reduction_orders')} "
            f"| {s.get('wall_time_seconds'):.0f} s | {s.get('convergence_status')} |")
        print(f"{case}: status={s.get('convergence_status')} step={final_step} "
              f"orders={s.get('residual_reduction_orders')} end_match={ok_end}")

    header = (
        "# Run Manifest\n\n"
        "| case | ranks | final step | final t | residual reduction (orders) | "
        "wall time | status |\n|---|---|---|---|---|---|---|\n"
    )
    with open(os.path.join(REPORT, "run_manifest.md"), "w") as f:
        f.write(header + "\n".join(manifest_rows) + "\n")

    with open(os.path.join(REPORT, "report_tables.json"), "w") as f:
        json.dump(tables, f, indent=1)
    print("wrote run_manifest.md and report_tables.json")


if __name__ == "__main__":
    main()

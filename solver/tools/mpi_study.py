#!/usr/bin/env python3
"""Rank-count study: timing, parallel efficiency, partition quality and force
consistency across MPI rank counts."""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import matplotlib.pyplot as plt  # noqa: E402
from plot_style import apply_style, SERIES  # noqa: E402


def collect(root):
    runs = {}
    for d in sorted(glob.glob(os.path.join(root, "*_np*"))):
        m = re.match(r"(.*)_np(\d+)$", os.path.basename(d))
        if not m or not os.path.exists(os.path.join(d, "metadata.json")):
            continue
        case, np_ = m.group(1), int(m.group(2))
        meta = json.load(open(os.path.join(d, "metadata.json")))
        status = json.load(open(os.path.join(d, "run_status.json")))
        pd = json.load(open(os.path.join(d, "partition_diagnostics.json")))
        runs.setdefault(case, {})[np_] = dict(meta=meta, status=status, part=pd, dir=d)
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="results/_mpi")
    ap.add_argument("--out-csv", default="report/mpi_study.csv")
    ap.add_argument("--out-figure", default="report/figures/mpi_scaling.png")
    args = ap.parse_args()

    runs = collect(args.root)
    if not runs:
        print(f"no rank-study runs found under {args.root}")
        return 1

    rows = []
    for case, by_np in sorted(runs.items()):
        ref_np = min(by_np)
        ref = by_np[ref_np]
        if ref_np != 1:
            print(f"warning: {case} has no np=1 run; speed-up and force differences are "
                  f"referenced to np={ref_np}")
        for np_, r in sorted(by_np.items()):
            p = r["part"]
            rows.append(dict(
                case_id=case, mpi_ranks=np_,
                steps=r["status"]["final_step"],
                wall_time_s=round(float(r["status"]["wall_time_seconds"]), 2),
                speedup=round(float(ref["status"]["wall_time_seconds"]) /
                              float(r["status"]["wall_time_seconds"]), 3),
                cl=float(r["meta"]["final_cl"]), cd=float(r["meta"]["final_cd"]),
                reference_ranks=ref_np,
                cd_rel_diff_vs_ref=abs(float(r["meta"]["final_cd"]) -
                                       float(ref["meta"]["final_cd"])) /
                                   max(abs(float(ref["meta"]["final_cd"])), 1e-30),
                cl_abs_diff_vs_ref=abs(float(r["meta"]["final_cl"]) -
                                       float(ref["meta"]["final_cl"])),
                residual_orders=round(float(r["status"]["residual_reduction_orders"]), 3),
                edge_cut=p["edge_cut"], min_owned=p["min_owned_cells"],
                max_owned=p["max_owned_cells"],
                load_balance=round(float(p["load_balance_ratio"]), 4),
                total_ghost=sum(x["num_cells_ghost"] for x in p["ranks"]),
                mean_neighbours=round(float(np.mean([x["num_neighbor_ranks"]
                                                     for x in p["ranks"]])), 2),
                total_halo_cells=sum(x["send_cells"] for x in p["ranks"]),
            ))

    os.makedirs(os.path.dirname(os.path.abspath(args.out_csv)), exist_ok=True)
    with open(args.out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {args.out_csv} ({len(rows)} runs)")

    apply_style()
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.2))
    for i0, (case, by_np) in enumerate(sorted(runs.items())):
        i = i0 % len(SERIES)
        nps = sorted(by_np)
        t = np.array([by_np[n]["status"]["wall_time_seconds"] for n in nps])
        cd = np.array([by_np[n]["meta"]["final_cd"] for n in nps])
        ec = np.array([by_np[n]["part"]["edge_cut"] for n in nps])
        gh = np.array([sum(x["num_cells_ghost"] for x in by_np[n]["part"]["ranks"])
                       for n in nps])
        axes[0].plot(nps, t[0] / t, marker="o", color=SERIES[i], label=case)
        # The reference rank count is its own reference, so its difference is
        # identically zero and is omitted rather than drawn at a fake floor.
        # The difference is shown in ABSOLUTE form so that it can be compared
        # with the absolute force-drift tolerance at which the runs stop; a
        # relative measure would exaggerate the aerofoil, whose inviscid drag
        # is itself only 1e-3.
        axes[1].plot(nps[1:], np.abs(cd[1:] - cd[0]), marker="s",
                     color=SERIES[i], label=case)
        axes[2].plot(nps, gh, marker="^", color=SERIES[i], label=f"{case}: ghost cells")
        axes[2].plot(nps, ec, marker="v", ls="--", color=SERIES[i], label=f"{case}: edge cut")
    axes[0].plot([1, 8], [1, 8], "k:", lw=1.2, label="ideal")
    axes[0].axvline(4, color="0.5", lw=1.1, ls="-.")
    axes[0].text(4.05, 1.05, "4-CPU quota", fontsize=8, color="0.35", rotation=90,
                 va="bottom")
    axes[0].set_xlabel("MPI ranks")
    axes[0].set_ylabel(r"speed-up $T_1/T_p$")
    axes[0].set_title("parallel speed-up")
    axes[0].legend(fontsize=8)
    axes[1].set_yscale("log")
    axes[1].set_xlabel("MPI ranks")
    axes[1].set_ylabel(r"$|C_D(p)-C_D(p_{\mathrm{ref}})|$")
    axes[1].set_title(r"force consistency across rank counts")
    axes[1].axhline(5.0e-5, color="0.4", ls=":", lw=1.2)
    axes[1].text(2.05, 5.6e-5, "force-stationarity tolerance", fontsize=7, color="0.35")
    axes[1].set_ylim(1e-7, 2e-4)
    axes[1].legend(fontsize=8)
    axes[2].set_yscale("log")
    axes[2].set_xlabel("MPI ranks")
    axes[2].set_ylabel("cells")
    axes[2].set_title("halo size and METIS edge cut")
    axes[2].legend(fontsize=7)
    for a in axes:
        a.set_xscale("log", base=2)
        a.set_xticks([1, 2, 4, 8])
        a.set_xticklabels(["1", "2", "4", "8"])
    fig.tight_layout()
    os.makedirs(os.path.dirname(os.path.abspath(args.out_figure)), exist_ok=True)
    fig.savefig(args.out_figure)
    plt.close(fig)
    print(f"wrote {args.out_figure}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

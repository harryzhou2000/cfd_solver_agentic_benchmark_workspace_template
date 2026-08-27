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

    # Rank-independent output discipline for the surface files.  (The field
    # file is checked separately, by tests/mpi_consistency.sh, which compares
    # its point and connectivity blocks and its cell ordering.)
    # What must be rank-independent is the *structure* of the file: the same rows
    # in the same order, describing the same boundary faces.  The solution
    # columns cannot be bit-identical, because different rank counts take
    # slightly different iteration paths and stop at different steps, so they
    # are held to a tolerance instead of to equality.  Both properties are
    # enforced -- structure exactly, solution against SOLUTION_TOL -- because a
    # check that is recorded but never compared to anything is not a check.
    SOLUTION_TOL = 5.0e-2   # relative, per column RMS
    GEOM = ["x", "y", "nx", "ny", "tag"]
    ri = {}
    for case, by_np in sorted(runs.items()):
        ref_np = min(by_np)
        base = by_np[ref_np]["dir"]
        for np_, r in sorted(by_np.items()):
            if np_ == ref_np:
                continue
            for fn in ("surface.csv", "surface_cellcenter.csv"):
                a, b = os.path.join(base, fn), os.path.join(r["dir"], fn)
                if not (os.path.exists(a) and os.path.exists(b)):
                    continue
                ra = list(csv.DictReader(open(a, newline="")))
                rb = list(csv.DictReader(open(b, newline="")))
                geom = [c for c in GEOM if ra and c in ra[0]]
                same_rows = len(ra) == len(rb)
                same_geom = same_rows and all(
                    all(x[c] == y[c] for c in geom) for x, y in zip(ra, rb))
                diffs, worst_at = [], None
                worst = 0.0
                scale = {}
                if same_rows:
                    for c in (ra[0] if ra else {}):
                        if c in geom:
                            continue
                        try:
                            col = np.array([float(x[c]) for x in ra])
                        except ValueError:
                            continue
                        # RMS of the column, floored by its own peak magnitude
                        # so a column that is identically zero cannot divide.
                        rms = float(np.sqrt((col**2).mean()))
                        scale[c] = max(rms, 1e-12)
                    for x, y in zip(ra, rb):
                        for c in x:
                            if c in geom:
                                continue
                            try:
                                fa, fb = float(x[c]), float(y[c])
                            except ValueError:
                                continue
                            # Scale each column by its own magnitude over the
                            # file, not by max(|value|, 1): a floor of 1 turns
                            # this into an absolute difference for every column
                            # whose values are below one -- cf, mach, the
                            # velocity components -- and these cases are
                            # non-dimensionalised so that is most of them.
                            d = abs(fa - fb) / scale.get(c, 1.0)
                            diffs.append(d)
                            if d > worst:
                                worst, worst_at = d, dict(column=c, x=float(x["x"]),
                                                          y=float(x["y"]))
                ri.setdefault(f"{case}:{fn}", {})[f"np{ref_np}_vs_np{np_}"] = dict(
                    same_row_count=bool(same_rows),
                    identical_geometry_and_order=bool(same_geom),
                    # None, not 0.0: a structurally mismatched pair has no
                    # meaningful solution difference and must not be recorded as
                    # a perfect one.
                    max_relative_solution_difference=(worst if same_rows else None),
                    solution_within_tolerance=bool(same_rows and worst <= SOLUTION_TOL),
                    tolerance=SOLUTION_TOL,
                    worst_at=worst_at,
                    # The maximum sits in the singular trailing-edge slivers of
                    # the aerofoil; the percentile shows the level everywhere
                    # else without special-casing a region.
                    p99_relative_solution_difference=(
                        float(np.percentile(diffs, 99)) if diffs else None),
                    median_relative_solution_difference=(
                        float(np.median(diffs)) if diffs else None))
    out = os.path.join(os.path.dirname(args.out_csv), "rank_independence.json")
    expected = sum(2 * (len(by_np) - 1) for by_np in runs.values())
    entries = [d for v in ri.values() for d in v.values()]
    summary = dict(
        comparisons=len(entries),
        comparisons_expected=expected,
        # Written unconditionally, so a run that compared nothing overwrites a
        # stale pass rather than leaving it in place for the submission gate.
        all_comparisons_ran=len(entries) == expected and expected > 0,
        structure_failures=[k for k, v in ri.items()
                            if not all(d["identical_geometry_and_order"]
                                       for d in v.values())],
        solution_failures=[k for k, v in ri.items()
                           if not all(d["solution_within_tolerance"] for d in v.values())],
        solution_tolerance=SOLUTION_TOL,
        max_relative_solution_difference=(
            max(d["max_relative_solution_difference"] for d in entries) if entries else None),
        p99_relative_solution_difference=(
            max(d["p99_relative_solution_difference"] for d in entries) if entries else None),
        note=("Solution columns are scaled by the RMS of that column, so the "
              "difference is genuinely relative. Geometry columns must be "
              "identical; solution columns must be within solution_tolerance."),
        comparisons_detail=ri)
    json.dump(summary, open(out, "w"), indent=2)
    print(f"wrote {out}: {summary['comparisons']}/{expected} comparisons, "
          f"structure {'ok' if not summary['structure_failures'] else 'FAILED'}, "
          f"solution {'ok' if not summary['solution_failures'] else 'FAILED'}, "
          f"max {summary['max_relative_solution_difference']}")

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

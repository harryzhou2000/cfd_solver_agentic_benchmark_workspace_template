#!/usr/bin/env python3
"""Aggregate per-case results into report/run_manifest.csv and a machine-
readable summary (report/results_summary.json), plus Re200 shedding analysis.

Usage: build_report_data.py --results-root <dir> --report-dir report \
          [--cases c1,c2,...] [--transient-start 200.0]
"""
import argparse, csv, json, math, os, sys
from pathlib import Path

import numpy as np

def _f(x):
    try:
        return float(x)
    except Exception:
        return None

def read_last_csv(path):
    if not os.path.exists(path):
        return None
    rows = list(csv.DictReader(open(path)))
    return rows[-1] if rows else None

def strouhal_from_cl(times, cls, t_start):
    """Dominant shedding frequency from post-transient cl(t) via FFT."""
    t = np.array(times); c = np.array(cls)
    m = t >= t_start
    t = t[m]; c = c[m]
    if len(t) < 64:
        return None, None, None
    dt = float(np.mean(np.diff(t)))
    c = c - np.mean(c)
    # Hann window to reduce leakage
    c = c * np.hanning(len(c))
    n = len(c)
    F = np.fft.rfft(c)
    freqs = np.fft.rfftfreq(n, d=dt)
    power = np.abs(F) ** 2
    if len(power) < 3:
        return None, None, None
    i = int(np.argmax(power[1:])) + 1  # skip DC
    f_peak = float(freqs[i])
    # cl amplitude: rms-based estimate of the oscillation amplitude
    t2 = np.array(times); c2 = np.array(cls)
    m2 = t2 >= t_start
    c2 = c2[m2]
    amp = float(np.sqrt(2.0 * np.mean((c2 - np.mean(c2)) ** 2)))
    cl_max = float(np.max(np.abs(c2 - np.mean(c2))))
    return f_peak, amp, cl_max

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-root", required=True)
    ap.add_argument("--report-dir", required=True)
    ap.add_argument("--cases", default=None)
    ap.add_argument("--transient-start", type=float, default=200.0)
    args = ap.parse_args()
    root = Path(args.results_root)
    rep = Path(args.report_dir)
    rep.mkdir(parents=True, exist_ok=True)
    if args.cases:
        case_ids = args.cases.split(",")
    else:
        case_ids = sorted([d.name for d in root.iterdir() if (d / "run_status.json").exists()])
    manifest_rows = []
    summary = {}
    for cid in case_ids:
        cdir = root / cid
        rs_path = cdir / "run_status.json"
        if not rs_path.exists():
            print("SKIP (no run_status):", cid); continue
        rs = json.load(open(rs_path))
        meta = {}
        mp = cdir / "metadata.json"
        if mp.exists():
            meta = json.load(open(mp))
        fl = read_last_csv(cdir / "forces.csv")
        row = {
            "case_id": cid,
            "command": rs.get("command", ""),
            "mpi_ranks": rs.get("mpi_ranks", meta.get("mpi_ranks", "")),
            "num_cells_global": meta.get("num_cells_global", ""),
            "final_step": rs.get("final_step", ""),
            "final_physical_time": rs.get("final_physical_time", ""),
            "wall_time_seconds": round(_f(rs.get("wall_time_seconds", 0.0)) or 0.0, 1),
            "convergence_status": rs.get("convergence_status", ""),
            "residual_reduction_orders": round(_f(rs.get("residual_reduction_orders", 0.0)) or 0.0, 3),
            "cd": round(_f(fl.get("cd", 0.0)), 5) if fl else "",
            "cl": round(_f(fl.get("cl", 0.0)), 5) if fl else "",
            "cmz": round(_f(fl.get("cmz", 0.0)), 5) if fl else "",
            "notes": rs.get("notes", ""),
        }
        entry = dict(row)
        # Re200 shedding analysis
        if "re200" in cid.lower() and fl is not None:
            fcsv = cdir / "forces.csv"
            rows = list(csv.DictReader(open(fcsv)))
            times = [_f(r["physical_time"]) for r in rows]
            cls = [_f(r["cl"]) for r in rows]
            cds = [_f(r["cd"]) for r in rows]
            f, amp, clmax = strouhal_from_cl(times, cls, args.transient_start)
            tarr = np.array(times); carr = np.array(cds)
            mm = tarr >= args.transient_start
            mean_cd = float(np.mean(carr[mm])) if mm.any() else None
            entry.update({"strouhal": (round(f, 4) if f else None),
                          "cl_amplitude": (round(amp, 4) if amp else None),
                          "mean_cd_posttransient": (round(mean_cd, 4) if mean_cd else None)})
            print(f"  {cid}: St={f}, cl_amp={amp}, mean_cd={mean_cd}")
        summary[cid] = entry
        manifest_rows.append(row)
    # write run_manifest.csv
    cols = ["case_id", "command", "mpi_ranks", "num_cells_global", "final_step",
            "final_physical_time", "wall_time_seconds", "convergence_status",
            "residual_reduction_orders", "cd", "cl", "cmz", "notes"]
    with open(rep / "run_manifest.csv", "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=cols)
        w.writeheader()
        for r in manifest_rows:
            w.writerow(r)
    json.dump(summary, open(rep / "results_summary.json", "w"), indent=2)
    print("wrote", rep / "run_manifest.csv", "and results_summary.json for",
          len(manifest_rows), "cases")

if __name__ == "__main__":
    main()

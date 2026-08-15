#!/usr/bin/env python3
"""Post-transient vortex-shedding statistics from a cylinder Re200 forces.csv.

Usage: analyze_re200.py <forces.csv> [--dt 0.01] [--start-time 150.0]
Prints mean CD/CL, CL amplitude, shedding frequency and Strouhal number.
"""

import csv
import math
import sys


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "results/cylinder_m010_laminar_re200/forces.csv"
    dt = 0.01
    start = 150.0
    for i, a in enumerate(sys.argv[2:]):
        if a == "--dt" and i + 2 < len(sys.argv):
            dt = float(sys.argv[i + 3])
        if a == "--start-time" and i + 2 < len(sys.argv):
            start = float(sys.argv[i + 3])
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    t = [float(r["physical_time"]) for r in rows]
    cl = [float(r["cl"]) for r in rows]
    cd = [float(r["cd"]) for r in rows]
    idx = [i for i in range(len(t)) if t[i] >= start]
    if len(idx) < 100:
        print(f"not enough post-transient samples: {len(idx)}")
        return 1
    clp = [cl[i] for i in idx]
    cdp = [cd[i] for i in idx]
    mean_cl = sum(clp) / len(clp)
    mean_cd = sum(cdp) / len(cdp)
    cl_amp = 0.5 * (max(clp) - min(clp))
    cl_rms = math.sqrt(sum((c - mean_cl) ** 2 for c in clp) / len(clp))
    # Dominant frequency via zero crossings of CL - mean.
    crossings = 0
    prev = clp[0] - mean_cl
    for v in clp[1:]:
        cur = v - mean_cl
        if prev * cur < 0:
            crossings += 1
        prev = cur
    span = t[idx[-1]] - t[idx[0]]
    freq = crossings / (2.0 * span) if span > 0 else 0.0
    st = freq  # St = f*D/U with D=1, U=1
    print(f"post-transient samples: {len(clp)} over t in [{t[idx[0]]:.2f}, {t[idx[-1]]:.2f}]")
    print(f"mean CD = {mean_cd:.5f}")
    print(f"mean CL = {mean_cl:.5f}, CL amplitude = {cl_amp:.5f}, CL RMS = {cl_rms:.5f}")
    print(f"shedding frequency f = {freq:.4f} (1/time), Strouhal = {st:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

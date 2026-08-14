#!/usr/bin/env python3
"""Fill the benchmark report placeholders from the run manifests and CSVs.

Usage:
  fill_report.py <results_dir> <report_dir> [--mpi-dir <np2_runs>]

Replaces the %% RUNSTATUS_ROWS, %% FORCE_ROWS, %% FIGURE_<case> and
\\X... macros in <report_dir>/report.tex with rows/blocks generated from
the solver outputs, so the report stays reproducible from the data.
"""

import argparse
import csv
import json
import math
import re
import sys
from pathlib import Path


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

SHORT = {
    "naca0012_m015_inviscid": "m015",
    "naca0012_m080_inviscid": "m080",
    "naca0012_m200_inviscid": "m200",
    "naca0012_m015_laminar_re5000": "m015l",
    "naca0012_m080_laminar_re5000": "m080l",
    "naca0012_m200_laminar_re5000": "m200l",
    "cylinder_m010_laminar_re20": "cyl20",
    "cylinder_m010_laminar_re200": "re200",
}

CAPTIONS = {
    "residual": "Residual history (volume-weighted L2)",
    "forces": "Force-coefficient history ($C_D$ and $C_L$)",
    "cp": "Surface pressure coefficient (upper/lower surface)",
    "cf": "Skin-friction coefficient (upper surface)",
    "mach": "Mach number field",
    "pressure": "Pressure field",
    "vorticity": "Wake vorticity, clipped to $[-5,5]$",
}


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def fmt(x, digits=4):
    return f"{float(x):.{digits}f}"


def figure_block(png, label, caption, width="0.95\\linewidth"):
    return (
        "\\begin{figure}[ht]\n"
        "\\centering\n"
        f"\\includegraphics[width={width}]{{figures/{png}}}\n"
        f"\\caption{{{caption}}}\n"
        f"\\label{{fig:{label}}}\n"
        "\\end{figure}\n"
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("results")
    ap.add_argument("report")
    ap.add_argument("--mpi-dir", default=None)
    args = ap.parse_args()
    results = Path(args.results)
    report = Path(args.report)
    tex_path = report / "report.tex"
    template = report / "report.template.tex"
    tex = template.read_text() if template.exists() else tex_path.read_text()

    run_manifest = list(read_csv(report / "run_manifest.csv"))
    sanity = json.loads((report / "sanity_checks.json").read_text())
    by_case = {c["case_id"]: c for c in run_manifest}

    # ---- run-status table ------------------------------------------------
    rows = []
    for case in CASES:
        s = by_case[case]
        rows.append(
            f"{case.replace('_', '\\_')} & {s['final_step']} & "
            f"{fmt(s['residual_reduction_orders'], 2)} & "
            f"{fmt(s['wall_time_seconds'], 1)} & "
            f"{s['convergence_status'].replace('_', '\\_')} \\\\"
        )
    tex = tex.replace("%% RUNSTATUS_ROWS", "\n".join(rows))

    # ---- force table -----------------------------------------------------
    rows = []
    for case in CASES:
        forces = read_csv(results / case / "forces.csv")
        if case.endswith("re200"):
            tail = forces[-2000:]
            cd = sum(float(r["cd"]) for r in tail) / len(tail)
            cl = sum(float(r["cl"]) for r in tail) / len(tail)
            last = tail[-1]
            note = "time-mean over last 2000 steps"
        else:
            tail = forces[-200:]
            last = forces[-1]
            cd = sum(float(r["cd"]) for r in tail) / len(tail)
            cl = sum(float(r["cl"]) for r in tail) / len(tail)
            note = "mean over last 200 steps"
        rows.append(
            f"{case.replace('_', '\\_')} & {fmt(cd)} & {fmt(cl)} & "
            f"{fmt(last['cmz'])} & {fmt(last['pressure_drag'])} & "
            f"{fmt(last['viscous_drag'])} {('(' + note + ')') if note else ''} \\\\"
        )
    tex = tex.replace("%% FORCE_ROWS", "\n".join(rows))

    # ---- figure blocks ---------------------------------------------------
    for case in CASES:
        short = SHORT[case]
        d = results / case
        blocks = []
        for kind in ["residual", "forces", "cp", "cf", "mach", "pressure"]:
            if kind == "cf" and "laminar" not in case:
                continue
            png = f"{case}_{kind}.png"
            if not (report / "figures" / png).exists():
                continue
            blocks.append(figure_block(png, f"{short}-{kind}", CAPTIONS[kind]))
        if case.endswith("re200"):
            png = f"{case}_vorticity.png"
            if (report / "figures" / png).exists():
                blocks.append(figure_block(png, f"{short}-vort", CAPTIONS["vorticity"]))
        tex = tex.replace(f"%% FIGURE_{short}\n", "\n".join(blocks) + "\n")

    # ---- MPI comparison figure blocks ------------------------------------
    if args.mpi_dir:
        blocks = []
        for case, short in [("naca0012_m015_inviscid", "m015"),
                            ("cylinder_m010_laminar_re20", "cyl20")]:
            for kind, label in [("residual", "res"), ("forces", "forces")]:
                png = f"mpi_{case}_{kind}.png"
                if (report / "figures" / png).exists():
                    blocks.append(figure_block(
                        png, f"mpi-{short}-{kind}",
                        f"MPI rank-count comparison, {case.replace('_', '\\_')}: "
                        f"{kind} history"))
        tex = tex.replace("%% FIGURE_mpi_compare\n", "\n".join(blocks) + "\n")

    # ---- Re200 vortex-street statistics ----------------------------------
    forces = read_csv(results / "cylinder_m010_laminar_re200" / "forces.csv")
    tail = [(float(r["step"]) * 0.01, float(r["cl"]), float(r["cd"]))
            for r in forces if float(r["step"]) >= 15000]
    ts = [t for t, _, _ in tail]
    cls = [cl for _, cl, _ in tail]
    cds = [cd for _, _, cd in tail]
    n = len(tail)
    mean_cd = sum(cds) / n
    mean_cl = sum(cls) / n
    amp = max(abs(c) - abs(mean_cl) for c in cls)
    # Dominant frequency of cl via FFT on the uniform post-transient series,
    # restricted to the physically meaningful St < 1 range (shedding
    # frequencies for this cylinder are O(0.1-0.3); the broadband high-
    # frequency content is numerical noise).
    dt = ts[1] - ts[0]
    freq = None
    if n > 64:
        import numpy as np
        y = np.array(cls)
        y = y - y.mean()
        sp = np.abs(np.fft.rfft(y * np.hanning(n)))
        f = np.fft.rfftfreq(n, dt)
        mask = (f >= 0.05) & (f < 0.5)
        spm = sp[mask]
        fm = f[mask]
        j = 1 + int(np.argmax(spm[1:]))
        freq = float(fm[j])
    st = freq if freq else float("nan")
    tex = tex.replace("\\XRE200CD", fmt(mean_cd))
    tex = tex.replace("\\XRE200CL", fmt(amp))
    tex = tex.replace("\\XRE200F", f"{freq:.3f}")
    tex = tex.replace("\\XRE200ST", f"{st:.3f}")

    # ---- MPI comparison ---------------------------------------------------
    if args.mpi_dir:
        mpi = Path(args.mpi_dir)
        parts = []
        for case in ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]:
            p8 = read_csv(results / case / "forces.csv")[-1]
            p2 = read_csv(mpi / case / "forces.csv")[-1]
            parts.append(
                f"{case.replace('_', '\\_')}: np=8 $C_D$={fmt(p8['cd'])}, "
                f"np=2 $C_D$={fmt(p2['cd'])}, "
                f"$\\Delta C_D$={fmt(abs(float(p8['cd'])-float(p2['cd'])), 5)}; "
                f"np=8 $C_L$={fmt(p8['cl'])}, np=2 $C_L$={fmt(p2['cl'])}"
            )
        tex = tex.replace("\\XMPICD", "; ".join(parts))

    tex_path.write_text(tex)
    print("report.tex filled")


if __name__ == "__main__":
    main()

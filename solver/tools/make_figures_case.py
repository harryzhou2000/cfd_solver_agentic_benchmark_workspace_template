#!/usr/bin/env python3
"""Generate report figures for one case output directory.

usage: make_figures_case.py <case-output-dir> <figures-dir> <case-json>
Writes residual/force history, surface distributions, Mach/pressure contours,
wake zooms, and (for transient) vorticity + shedding analysis plots. Also
appends rows to <figures-dir>/figure_manifest.csv.
"""
import json
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cfdplot import read_vtu, cell_vorticity, contour, lineplot


def main():
    case_dir, fig_dir, case_json = sys.argv[1], sys.argv[2], sys.argv[3]
    case = json.load(open(case_json))
    cid = case["case_id"]
    os.makedirs(fig_dir, exist_ok=True)
    manifest = []

    res = np.genfromtxt(os.path.join(case_dir, "residuals.csv"),
                        delimiter=",", names=True)
    foc = np.genfromtxt(os.path.join(case_dir, "forces.csv"),
                        delimiter=",", names=True)
    keep = np.ones(len(foc), bool)
    keep[1:] = np.diff(foc["step"]) != 0
    foc = foc[keep]
    srf = np.genfromtxt(os.path.join(case_dir, "surface.csv"),
                        delimiter=",", names=True, dtype=None,
                        encoding=None)
    transient = case["run_control"]["type"] == "transient"
    is_cyl = "cylinder" in cid
    viscous = case["physics"]["mode"] == "laminar"

    x = res["physical_time"] if transient else res["step"]
    xlab = "physical time" if transient else "pseudo step"
    f1 = f"{cid}_residual_history.png"
    lineplot(x, [res["residual_l2"]], ["L2 residual"],
             os.path.join(fig_dir, f1), f"{cid}: residual history", xlab,
             "RMS residual", logy=True)
    manifest.append((f1, cid, "line", "residual_l2", "residuals.csv",
                     "L2 residual history"))

    f2 = f"{cid}_force_history.png"
    lineplot(x, [foc["cd"], foc["cl"]], ["$C_D$", "$C_L$"],
             os.path.join(fig_dir, f2), f"{cid}: force history", xlab,
             "force coefficient")
    manifest.append((f2, cid, "line", "cl,cd", "forces.csv",
                     "lift and drag history"))

    tag = srf["tag"].astype(str)
    f3 = f"{cid}_surface_cp.png"
    fig, ax = plt.subplots(figsize=(8, 5))
    if is_cyl:
        th = np.degrees(np.arctan2(srf["y"], srf["x"]))
        s = np.argsort(th)
        ax.plot(th[s], srf["cp"][s], ".", ms=2.5, label="wall $C_p$")
        ax.set_xlabel("azimuth [deg]")
    else:
        up = srf["y"] > 0
        for m, lb in [(up, "upper"), (~up, "lower")]:
            s = np.argsort(srf["x"][m])
            ax.plot(srf["x"][m][s], srf["cp"][m][s], ".", ms=2.5, label=lb)
        ax.invert_yaxis()
        ax.set_xlabel("x/c")
    ax.set_ylabel("$C_p$")
    ax.set_title(f"{cid}: surface pressure coefficient")
    ax.grid(True, alpha=0.4)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(fig_dir, f3), dpi=140)
    plt.close(fig)
    manifest.append((f3, cid, "line", "cp", "surface.csv",
                     "surface pressure coefficient"))

    if viscous:
        f4 = f"{cid}_surface_cf.png"
        fig, ax = plt.subplots(figsize=(8, 5))
        if is_cyl:
            th = np.degrees(np.arctan2(srf["y"], srf["x"]))
            s = np.argsort(th)
            ax.plot(th[s], srf["cf"][s], ".", ms=2.5)
            ax.set_xlabel("azimuth [deg]")
        else:
            up = srf["y"] > 0
            for m, lb in [(up, "upper"), (~up, "lower")]:
                s = np.argsort(srf["x"][m])
                ax.plot(srf["x"][m][s], srf["cf"][m][s], ".", ms=2.5,
                        label=lb)
            ax.set_xlabel("x/c")
        ax.set_ylabel("$C_f$")
        ax.set_title(f"{cid}: surface skin friction")
        ax.grid(True, alpha=0.4)
        ax.legend()
        fig.tight_layout()
        fig.savefig(os.path.join(fig_dir, f4), dpi=140)
        plt.close(fig)
        manifest.append((f4, cid, "line", "cf", "surface.csv",
                         "surface skin friction"))

    pts, tris, cellidx, fields = read_vtu(
        os.path.join(case_dir, "field_final.vtu"))
    ccx = pts[tris].mean(axis=1)
    cc = np.hypot(ccx[:, 0], ccx[:, 1])

    if is_cyl:
        zoom = (cc < 4.0)
        xlim, ylim = (-2.5, 6.0), (-4.0, 4.0)
    else:
        zoom = (np.abs(ccx[:, 1]) < 1.0) & (ccx[:, 0] > -1.0) & (ccx[:, 0] < 2.5)
        xlim, ylim = (-0.6, 2.0), (-0.9, 0.9)

    for var, nm in [("Mach", "mach"), ("Pressure", "pressure")]:
        f5 = f"{cid}_{nm}.png"
        contour(pts, tris, fields[var][cellidx],
                os.path.join(fig_dir, f5), f"{cid}: {var}", xlim=xlim,
                ylim=ylim)
        manifest.append((f5, cid, "contour", var.lower(), "field_final.vtu",
                         f"{var} contour near body"))
        f6 = f"{cid}_{nm}_full.png"
        contour(pts, tris, fields[var][cellidx],
                os.path.join(fig_dir, f6), f"{cid}: {var} (full domain)")
        manifest.append((f6, cid, "contour", var.lower(), "field_final.vtu",
                         f"{var} contour full domain"))

    if is_cyl:
        f7 = f"{cid}_velocity_magnitude.png"
        vmag = np.hypot(fields["VelocityX"], fields["VelocityY"])[cellidx]
        contour(pts, tris, vmag, os.path.join(fig_dir, f7),
                f"{cid}: velocity magnitude", xlim=xlim, ylim=ylim)
        manifest.append((f7, cid, "contour", "velocity_magnitude",
                         "field_final.vtu", "velocity magnitude near body"))

    if transient:
        om = cell_vorticity(pts, tris, cellidx, fields["VelocityX"],
                            fields["VelocityY"])
        f8 = f"{cid}_vorticity.png"
        contour(pts, tris, om, os.path.join(fig_dir, f8),
                f"{cid}: vorticity", cmap="RdBu_r", clim=(-5.0, 5.0),
                xlim=xlim, ylim=ylim)
        manifest.append((f8, cid, "contour", "vorticity", "field_final.vtu",
                         "vorticity clipped [-5,5], wake view"))

        t = foc["physical_time"]
        n0 = int(0.5 * len(t))
        cl = foc["cl"][n0:]
        tt = t[n0:]
        cl = cl - cl.mean()
        dt = t[1] - t[0]
        spec = np.abs(np.fft.rfft(cl * np.hanning(len(cl))))
        freqs = np.fft.rfftfreq(len(cl), d=dt)
        imax = 1 + np.argmax(spec[1:])
        f9 = f"{cid}_lift_spectrum.png"
        lineplot(freqs, [spec], ["|$C_L$| spectrum"],
                 os.path.join(fig_dir, f9),
                 f"{cid}: lift spectrum (peak f={freqs[imax]:.4f})",
                 "frequency", "amplitude")
        manifest.append((f9, cid, "line", "lift_spectrum", "forces.csv",
                         "lift-coefficient frequency spectrum"))
        f10 = f"{cid}_cl_tail.png"
        lineplot(tt, [cl], ["$C_L$"], os.path.join(fig_dir, f10),
                 f"{cid}: lift coefficient (post-transient)", "physical time",
                 "$C_L$")
        manifest.append((f10, cid, "line", "cl", "forces.csv",
                         "post-transient lift oscillation"))

    mpath = os.path.join(fig_dir, "figure_manifest.csv")
    import csv
    exists = os.path.exists(mpath)
    with open(mpath, "a", newline="") as fh:
        w = csv.writer(fh)
        if not exists:
            w.writerow(["figure_file", "case_id", "figure_type", "variable",
                        "source_file", "caption"])
        for row in manifest:
            w.writerow(row)
    print(f"figures written for {cid}: {len(manifest)}")


if __name__ == "__main__":
    main()

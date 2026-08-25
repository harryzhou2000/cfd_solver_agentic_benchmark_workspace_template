#!/usr/bin/env python3
"""Generate all report figures and the figure manifest from case outputs.

Usage:
  plot_results.py [--results-dir DIR] [--out DIR] [--cases id1,id2,...]

Paths default relative to the solver repo root. Missing case directories are
skipped with a warning so the script can be re-run as runs finish.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cfdplot import load_vtu, cells_to_triangles, cell_to_node, read_csv_cols  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

ALL_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def is_cylinder(case_id: str) -> bool:
    return case_id.startswith("cylinder")


def crop_window(case_id: str):
    if is_cylinder(case_id):
        return (-6.0, 14.0, -6.0, 6.0)
    return (-0.75, 2.25, -1.5, 1.5)


def contour(points, tris, nodal, case_id, varname, title, outfile,
            levels=41, clip=None):
    x0, x1, y0, y1 = crop_window(case_id)
    fig, ax = plt.subplots(figsize=(7, 4.2))
    if clip is not None:
        lv = np.linspace(clip[0], clip[1], levels)
        cs = ax.tricontourf(points[:, 0], points[:, 1], tris, nodal,
                            levels=lv, extend="both")
    else:
        cs = ax.tricontourf(points[:, 0], points[:, 1], tris, nodal,
                            levels=levels)
    cb = fig.colorbar(cs, ax=ax)
    cb.set_label(varname)
    ax.set_xlim(x0, x1)
    ax.set_ylim(y0, y1)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(title)
    fig.tight_layout()
    fig.savefig(outfile, dpi=150)
    plt.close(fig)


def plot_case(case_id: str, results_dir: Path, out_dir: Path,
              manifest_rows: list) -> dict | None:
    d = results_dir / case_id
    if not (d / "run_status.json").exists():
        print(f"SKIP {case_id}: no run_status.json", file=sys.stderr)
        return None
    status = json.loads((d / "run_status.json").read_text())
    transient = "re200" in case_id

    # ---- residual history ----
    res = read_csv_cols(d / "residuals.csv")
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    xax = res["physical_time"] if transient else res["step"]
    for eq, style in zip(["rho", "rhou", "rhov", "rhoE"],
                         [":", ":", ":", ":"]):
        v = np.maximum(res[eq], 1e-300)
        ax.semilogy(xax, v, style, alpha=0.55, label=eq)
    ax.semilogy(xax, np.maximum(res["residual_l2"], 1e-300), "k-",
                lw=1.6, label="residual_l2")
    ax.set_xlabel("physical_time" if transient else "step")
    ax.set_ylabel("residual (RMS)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    ax.set_title(f"{case_id}: residual history")
    fig.tight_layout()
    fname = f"residuals_{case_id}.png"
    fig.savefig(out_dir / fname, dpi=150)
    plt.close(fig)
    manifest_rows.append(dict(
        figure_file=fname, case_id=case_id, figure_type="residual_history",
        variable="residual_l2", source_file=f"results/{case_id}/residuals.csv",
        caption=f"Residual history for {case_id} (RMS of conservative-variable "
                f"residuals; bold line is the combined L2 norm)."))

    # ---- force history ----
    forces = read_csv_cols(d / "forces.csv")
    fig, axs = plt.subplots(2, 1, figsize=(6.4, 5.6), sharex=True)
    xax = forces["physical_time"] if transient else forces["step"]
    axs[0].plot(xax, forces["cd"], lw=0.8)
    axs[0].set_ylabel("cd")
    axs[0].grid(True, alpha=0.3)
    axs[1].plot(xax, forces["cl"], lw=0.8, color="tab:red")
    axs[1].set_ylabel("cl")
    axs[1].set_xlabel("physical_time" if transient else "step")
    axs[1].grid(True, alpha=0.3)
    axs[0].set_title(f"{case_id}: force history")
    fig.tight_layout()
    fname = f"forces_{case_id}.png"
    fig.savefig(out_dir / fname, dpi=150)
    plt.close(fig)
    manifest_rows.append(dict(
        figure_file=fname, case_id=case_id, figure_type="force_history",
        variable="cd,cl", source_file=f"results/{case_id}/forces.csv",
        caption=f"Force-coefficient history for {case_id}."))

    # ---- surface distribution ----
    surf = read_csv_cols(d / "surface.csv")
    fig, axs = plt.subplots(1, 2, figsize=(11, 4.2))
    if is_cylinder(case_id):
        theta = np.degrees(np.arctan2(surf["y"], surf["x"]))
        order = np.argsort(theta)
        axs[0].plot(theta[order], surf["cp"][order], ".", ms=2)
        axs[0].set_xlabel("theta [deg]")
        axs[0].set_ylabel("cp")
        axs[0].invert_yaxis()
        axs[0].grid(True, alpha=0.3)
        axs[0].set_title("wall pressure coefficient")
        axs[1].plot(theta[order], surf["cf"][order], ".", ms=2,
                    color="tab:red")
        axs[1].set_xlabel("theta [deg]")
        axs[1].set_ylabel("cf (tangential)")
        axs[1].grid(True, alpha=0.3)
        axs[1].set_title("wall skin friction")
    else:
        upper = surf["ny"] > 0
        for mask, lbl, mk in [(upper, "suction side (ny>0)", "."),
                              (~upper, "pressure side (ny<0)", ".")]:
            order = np.argsort(surf["x"][mask])
            axs[0].plot(surf["x"][mask][order], surf["cp"][mask][order],
                        mk, ms=2, label=lbl)
        axs[0].invert_yaxis()
        axs[0].set_xlabel("x/c")
        axs[0].set_ylabel("cp")
        axs[0].legend()
        axs[0].grid(True, alpha=0.3)
        axs[0].set_title("surface pressure coefficient")
        for mask, lbl in [(upper, "suction side"), (~upper, "pressure side")]:
            order = np.argsort(surf["x"][mask])
            axs[1].plot(surf["x"][mask][order], surf["cf"][mask][order],
                        ".", ms=2, label=lbl)
        axs[1].set_xlabel("x/c")
        axs[1].set_ylabel("cf (tangential)")
        axs[1].legend()
        axs[1].grid(True, alpha=0.3)
        axs[1].set_title("surface skin friction")
    fig.suptitle(f"{case_id}: surface distributions")
    fig.tight_layout()
    fname = f"surface_cp_{case_id}.png"
    fig.savefig(out_dir / fname, dpi=150)
    plt.close(fig)
    manifest_rows.append(dict(
        figure_file=fname, case_id=case_id, figure_type="surface_distribution",
        variable="cp,cf", source_file=f"results/{case_id}/surface.csv",
        caption=f"Surface pressure and skin-friction distributions for "
                f"{case_id}."))

    # ---- field contours ----
    vtu = d / "field_final.vtu"
    points, cells, cdata = load_vtu(vtu)
    tris = cells_to_triangles(cells)
    src = f"results/{case_id}/field_final.vtu"

    mach_n = cell_to_node(points, cells, cdata["mach"])
    fname = f"field_mach_{case_id}.png"
    contour(points, tris, mach_n, case_id, "Mach",
            f"{case_id}: Mach number field", out_dir / fname)
    manifest_rows.append(dict(
        figure_file=fname, case_id=case_id, figure_type="field_contour",
        variable="mach", source_file=src,
        caption=f"Mach-number field for {case_id} (cell values averaged to "
                f"nodes)."))

    p_n = cell_to_node(points, cells, cdata["pressure"])
    fname = f"field_pressure_{case_id}.png"
    contour(points, tris, p_n, case_id, "pressure",
            f"{case_id}: pressure field", out_dir / fname)
    manifest_rows.append(dict(
        figure_file=fname, case_id=case_id, figure_type="field_contour",
        variable="pressure", source_file=src,
        caption=f"Static-pressure field for {case_id}."))

    if is_cylinder(case_id):
        velmag = np.hypot(cdata["u"], cdata["v"])
        vm_n = cell_to_node(points, cells, velmag)
        fname = f"field_velmag_{case_id}.png"
        contour(points, tris, vm_n, case_id, "|u|",
                f"{case_id}: velocity magnitude", out_dir / fname)
        manifest_rows.append(dict(
            figure_file=fname, case_id=case_id, figure_type="field_contour",
            variable="velocity_magnitude", source_file=src,
            caption=f"Velocity-magnitude field for {case_id}."))

    if transient:
        # Vorticity wake visualization, clipped per case recommendation.
        if "vorticity" in cdata:
            vort = cdata["vorticity"]
        else:
            vort = compute_vorticity(points, cells, tris, cdata)
        vort_n = cell_to_node(points, cells, vort)
        fname = f"wake_vorticity_{case_id}.png"
        x0, x1, y0, y1 = (-2.0, 12.0, -4.0, 4.0)
        fig, ax = plt.subplots(figsize=(8, 3.6))
        cs = ax.tricontourf(points[:, 0], points[:, 1], tris, vort_n,
                            levels=np.linspace(-5.0, 5.0, 41), extend="both")
        cb = fig.colorbar(cs, ax=ax)
        cb.set_label("vorticity")
        ax.set_xlim(x0, x1)
        ax.set_ylim(y0, y1)
        ax.set_aspect("equal")
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.set_title(f"{case_id}: wake vorticity (clipped to [-5, 5])")
        fig.tight_layout()
        fig.savefig(out_dir / fname, dpi=150)
        plt.close(fig)
        manifest_rows.append(dict(
            figure_file=fname, case_id=case_id, figure_type="wake_vorticity",
            variable="vorticity", source_file=src,
            caption="Post-transient wake vorticity for the Re 200 vortex "
                    "street (clipped to [-5, 5])."))

    return status


def compute_vorticity(points, cells, tris, cdata):
    """Cell-centered vorticity dv/dx - du/dy from linear triangle gradients
    of nodal-averaged velocity, area-averaged back to cells."""
    u_n = cell_to_node(points, cells, cdata["u"])
    v_n = cell_to_node(points, cells, cdata["v"])
    tri_pts = points[tris]  # [ntri, 3, 3]
    x = tri_pts[:, :, 0]
    y = tri_pts[:, :, 1]
    det = (x[:, 1] - x[:, 0]) * (y[:, 2] - y[:, 0]) - \
        (x[:, 2] - x[:, 0]) * (y[:, 1] - y[:, 0])
    det = np.where(np.abs(det) < 1e-30, 1e-30, det)
    # b_i = dN_i/dx, c_i = dN_i/dy for linear triangles
    b = np.stack([y[:, 1] - y[:, 2], y[:, 2] - y[:, 0],
                  y[:, 0] - y[:, 1]], axis=1) / det[:, None]
    c = np.stack([x[:, 2] - x[:, 1], x[:, 0] - x[:, 2],
                  x[:, 1] - x[:, 0]], axis=1) / det[:, None]
    ut = u_n[tris]
    vt = v_n[tris]
    dudx = np.einsum("ij,ij->i", b, ut)
    dvdx = np.einsum("ij,ij->i", b, vt)
    dudy = np.einsum("ij,ij->i", c, ut)
    omega_tri = dvdx - dudy
    # Map triangle values back to the nearest original cell: build cell ->
    # triangles by fan order (cells_to_triangles fans tri->1, quad->2).
    omega_cell = np.zeros(len(cells))
    ti = 0
    for i, cl in enumerate(cells):
        ntri = 1 if len(cl) == 3 else len(cl) - 2
        omega_cell[i] = np.mean(omega_tri[ti:ti + ntri])
        ti += ntri
    return omega_cell


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results-dir", default=str(ROOT / "results"))
    ap.add_argument("--out", default=str(ROOT / "report" / "figures"))
    ap.add_argument("--cases", default=",".join(ALL_CASES))
    args = ap.parse_args()

    results_dir = Path(args.results_dir)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cases = [c for c in args.cases.split(",") if c]

    manifest_rows: list = []
    summaries = []
    for cid in cases:
        st = plot_case(cid, results_dir, out_dir, manifest_rows)
        if st is not None:
            summaries.append((cid, st))

    manifest = ROOT / "report" / "figure_manifest.csv"
    with open(manifest, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file", "case_id",
                                          "figure_type", "variable",
                                          "source_file", "caption"])
        w.writeheader()
        for row in manifest_rows:
            w.writerow(row)
    print(f"wrote {manifest} ({len(manifest_rows)} figures)")

    print("\nper-case summary:")
    for cid, st in summaries:
        print(f"  {cid:36s} {st['convergence_status']:22s} "
              f"steps={st['final_step']:>6} "
              f"orders={st['residual_reduction_orders']:.2f} "
              f"t_final={st['final_physical_time']:.2f} "
              f"wall={st['wall_time_seconds']:.1f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())

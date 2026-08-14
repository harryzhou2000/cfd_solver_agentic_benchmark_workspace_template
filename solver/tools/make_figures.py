#!/usr/bin/env python3
"""Generate publication-style figures from one case output directory.

Every plotted variable is named in the filename and in the optional manifest;
field views use actual VTU triangles, never point-only scatter plots.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np

from vtu_io import read_vtu

plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": 0.25,
                     "lines.linewidth": 1.5, "figure.dpi": 140})


def line_figure(case: str, out: Path, data: Path, kind: str) -> None:
    with data.open(newline="") as f:
        rows = list(csv.DictReader(f))
    xkey = "physical_time" if kind == "force" else "step"
    x = np.asarray([float(r[xkey]) for r in rows])
    fig, ax = plt.subplots(figsize=(6.4, 3.7), constrained_layout=True)
    if kind == "force":
        for key, label in (("cd", "$C_D$"), ("cl", "$C_L$")):
            ax.plot(x, [float(r[key]) for r in rows], label=label)
        ax.set_xlabel("Physical time"); ax.set_ylabel("Force coefficient")
        ax.legend(frameon=False, ncol=2)
        name = f"{case}_forces.png"
        caption = "Force coefficients $C_D$ and $C_L$ from forces.csv"
    else:
        ax.semilogy(x, [max(float(r["residual_l2"]), 1e-300) for r in rows],
                    label=r"$L_2$ residual")
        ax.semilogy(x, [max(float(r["residual_linf"]), 1e-300) for r in rows],
                    label=r"$L_\infty$ residual")
        ax.set_xlabel("Nonlinear step"); ax.set_ylabel("Global residual norm")
        ax.legend(frameon=False)
        name = f"{case}_residuals.png"
        caption = "Global residual norms from residuals.csv"
    ax.set_title(case); fig.savefig(out / name); plt.close(fig)
    return name, caption


def field_figure(case: str, out: Path, field: Path, variable: str) -> tuple[str, str]:
    mesh = read_vtu(field)
    if variable not in mesh.cell_data:
        raise ValueError(f"{variable} missing from {field}")
    tri = mtri.Triangulation(mesh.points[:, 0], mesh.points[:, 1], mesh.triangulated_connectivity())
    vals = mesh.cell_data[variable]
    # Build explicit cell-to-triangle expansion (triangles are appended in order).
    tri_vals = []
    for c, ctype in enumerate(mesh.types):
        tri_vals.extend([vals[c]] * (1 if ctype == 5 else 2))
    tri_vals = np.asarray(tri_vals)
    fig, ax = plt.subplots(figsize=(6.4, 4.0), constrained_layout=True)
    vmin, vmax = np.nanpercentile(tri_vals, [1, 99])
    if not np.isfinite(vmin) or vmin == vmax:
        vmin, vmax = float(np.nanmin(tri_vals)), float(np.nanmax(tri_vals) + 1e-12)
    tpc = ax.tripcolor(tri, tri_vals, shading="flat", cmap="viridis", vmin=vmin, vmax=vmax)
    cb = fig.colorbar(tpc, ax=ax); cb.set_label(variable)
    ax.set_aspect("equal", adjustable="box"); ax.set_xlabel("x"); ax.set_ylabel("y")
    ax.set_title(f"{case}: {variable}")
    name = f"{case}_{variable}.png"
    fig.savefig(out / name); plt.close(fig)
    return name, f"Filled unstructured-cell contour of {variable} from field_final.vtu"

def surface_figure(case: str, out: Path, data: Path) -> tuple[str, str]:
    rows = list(csv.DictReader(data.open(newline="")))
    x = np.asarray([float(r["x"]) for r in rows])
    cp = np.asarray([float(r["cp"]) for r in rows])
    fig, ax = plt.subplots(figsize=(6.4, 3.7), constrained_layout=True)
    order = np.argsort(x)
    ax.plot(x[order], cp[order])
    ax.set_xlabel("x / c")
    ax.set_ylabel("$C_p$")
    ax.set_title(case)
    name = f"{case}_surface_cp.png"
    fig.savefig(out / name); plt.close(fig)
    return name, "Surface pressure coefficient from surface.csv"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("case_id")
    ap.add_argument("case_dir", type=Path)
    ap.add_argument("--out-dir", type=Path, default=Path("report/figures"))
    args = ap.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, str]] = []
    def add(fig, typ, var, source, caption):
        entries.append({"figure_file": fig, "case_id": args.case_id, "figure_type": typ,
                        "variable": var, "source_file": str(source), "caption": caption})
    f = args.case_dir / "field_final.vtu"
    for var in ("mach", "pressure"):
        name, cap = field_figure(args.case_id, args.out_dir, f, var)
        add(name, "field_contour", var, str(f), cap)
    name, cap = line_figure(args.case_id, args.out_dir, args.case_dir / "residuals.csv", "residual")
    add(name, "residual_history", "residual_l2,residual_linf", str(args.case_dir / "residuals.csv"), cap)
    name, cap = line_figure(args.case_id, args.out_dir, args.case_dir / "forces.csv", "force")
    add(name, "force_history", "cd,cl", str(args.case_dir / "forces.csv"), cap)
    # Surface distributions required for every case (Cp always; cf for viscous).
    surf = args.case_dir / "surface.csv"
    if surf.exists():
        name, cap = surface_figure(args.case_id, args.out_dir, surf)
        add(name, "surface_distribution", "cp", str(surf), cap)
    if "re200" in args.case_id.lower() and "vorticity" in read_vtu(f).cell_data:
        name, cap = field_figure(args.case_id, args.out_dir, f, "vorticity")
        add(name, "wake_contour", "vorticity", str(f), cap)
    manifest = args.out_dir.parent / "figure_manifest.csv"
    exists = manifest.exists()
    with manifest.open("a", newline="") as out:
        w = csv.DictWriter(out, fieldnames=["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"])
        if not exists: w.writeheader()
        w.writerows(entries)


if __name__ == "__main__":
    main()

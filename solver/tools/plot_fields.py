#!/usr/bin/env python3
"""Render field visualizations from field_final.vtk (publication style).

Reads the solver's ASCII VTK output (unstructured grid, cell data) and
produces filled-contour (tricontourf/tripcolor) figures:

    <figdir>/<case_id>_mach.png        Mach number contours
    <figdir>/<case_id>_pressure.png    pressure contours
    <figdir>/<case_id>_velocity_mag.png  velocity magnitude (cylinder cases)
    <figdir>/<case_id>_vorticity.png   z-vorticity, clipped [-5, 5] (Re200)

View windows: near-body zoom for NACA airfoils (x in [-0.5, 1.5],
y in [-1, 1]); wake zoom for cylinder cases (x in [-2, 8], y in [-5, 5])
so the body and wake remain readable inside the radius-80 farfield.
Pressure and velocity-magnitude ranges are clipped to the 0.5-99.5
percentiles so a few outliers cannot collapse the color range.

Usage:
    python3 plot_fields.py RESULT_DIR... --figdir FIGDIR
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

DPI = 200

# View windows: (xlim, ylim) per geometry family.
NACA_VIEW = ((-0.5, 1.5), (-1.0, 1.0))
CYLINDER_VIEW = ((-2.0, 8.0), (-5.0, 5.0))

PRESSURE_CMAP = "inferno"
MACH_CMAP = "viridis"
VELOCITY_CMAP = "plasma"
VORTICITY_CMAP = "RdBu_r"
VORTICITY_CLIP = (-5.0, 5.0)  # documented clipped range for the Re200 wake

plt.rcParams.update(
    {
        "font.size": 12,
        "axes.labelsize": 13,
        "axes.titlesize": 13,
        "xtick.labelsize": 11,
        "ytick.labelsize": 11,
        "figure.dpi": DPI,
        "savefig.dpi": DPI,
        "savefig.bbox": "tight",
    }
)


def parse_vtk(path: Path) -> dict:
    """Parse the solver's ASCII unstructured-grid VTK file."""
    lines = path.read_text().splitlines()
    i = 0
    n = len(lines)
    data: dict = {}
    conn: list[list[int]] = []

    def next_line():
        nonlocal i
        line = lines[i]
        i += 1
        return line

    while i < n:
        line = lines[i]
        if line.startswith("POINTS"):
            npts = int(line.split()[1])
            i += 1
            pts = np.array(
                [list(map(float, lines[i + k].split()[:2])) for k in range(npts)]
            )
            data["points"] = pts
            i += npts
        elif line.startswith("CELLS"):
            _, ncell, _ = line.split()
            ncell = int(ncell)
            i += 1
            conn = []
            for _ in range(ncell):
                parts = list(map(int, lines[i].split()))
                conn.append(parts[1:])
                i += 1
            data["cells"] = conn
        elif line.startswith("CELL_TYPES"):
            i += 2
        elif line.startswith("CELL_DATA"):
            i += 1
        elif line.startswith("SCALARS"):
            name = line.split()[1]
            i += 2  # skip LOOKUP_TABLE
            vals = np.array([float(lines[i + k]) for k in range(len(conn))])
            data[name] = vals
            i += len(conn)
        elif line.startswith("VECTORS"):
            name = line.split()[1]
            i += 1
            vecs = np.array(
                [list(map(float, lines[i + k].split()[:2])) for k in range(len(conn))]
            )
            data[name] = vecs
            i += len(conn)
        else:
            i += 1
    return data


def build_triangles(cells: list[list[int]]) -> tuple[np.ndarray, np.ndarray]:
    """Split quads into two triangles.

    Returns (n_tri, 3) node-index array and the per-triangle values index
    (which original cell each triangle belongs to).
    """
    tris = []
    cell_of = []
    for k, c in enumerate(cells):
        if len(c) == 3:
            tris.append(c)
            cell_of.append(k)
        elif len(c) == 4:
            tris.append([c[0], c[1], c[2]])
            tris.append([c[0], c[2], c[3]])
            cell_of.extend([k, k])
    return np.array(tris, dtype=int), np.array(cell_of, dtype=int)


def node_average_velocity(data: dict) -> tuple[np.ndarray, np.ndarray]:
    """Node velocity as the unweighted average of the incident cell velocities."""
    pts = data["points"]
    cells = data["cells"]
    vel = data["velocity"]
    u = np.zeros(len(pts))
    v = np.zeros(len(pts))
    cnt = np.zeros(len(pts))
    for k, c in enumerate(cells):
        for idx in c:
            u[idx] += vel[k, 0]
            v[idx] += vel[k, 1]
            cnt[idx] += 1.0
    ok = cnt > 0.0
    u[ok] /= cnt[ok]
    v[ok] /= cnt[ok]
    return u, v


def cell_vorticity(data: dict) -> np.ndarray:
    """Per-cell z-vorticity from the cell-centered velocity field.

    A piecewise-linear fit over each triangle (vertices carry the
    node-averaged velocity) gives the local gradient; the triangle values
    are area-weighted back onto the parent cells. For quads this is the
    standard two-triangle decomposition of a bilinear interpolation.
    """
    pts = data["points"]
    tris, cell_of = build_triangles(data["cells"])
    u, v = node_average_velocity(data)

    x0 = pts[tris[:, 0], 0]
    y0 = pts[tris[:, 0], 1]
    dx1 = pts[tris[:, 1], 0] - x0
    dy1 = pts[tris[:, 1], 1] - y0
    dx2 = pts[tris[:, 2], 0] - x0
    dy2 = pts[tris[:, 2], 1] - y0
    det = dx1 * dy2 - dy1 * dx2
    safe = np.abs(det) > 1e-30

    du1 = u[tris[:, 1]] - u[tris[:, 0]]
    du2 = u[tris[:, 2]] - u[tris[:, 0]]
    dv1 = v[tris[:, 1]] - v[tris[:, 0]]
    dv2 = v[tris[:, 2]] - v[tris[:, 0]]

    with np.errstate(divide="ignore", invalid="ignore"):
        du_dy = np.where(safe, (dx1 * du2 - dx2 * du1) / det, 0.0)
        dv_dx = np.where(safe, (dv1 * dy2 - dv2 * dy1) / det, 0.0)
    tri_omega = dv_dx - du_dy
    areas = 0.5 * np.abs(det)

    n_cell = len(data["cells"])
    vort = np.zeros(n_cell)
    area_cell = np.zeros(n_cell)
    np.add.at(vort, cell_of, tri_omega * areas)
    np.add.at(area_cell, cell_of, areas)
    ok = area_cell > 0.0
    vort[ok] /= area_cell[ok]
    return vort


def velocity_magnitude(data: dict) -> np.ndarray:
    vel = data["velocity"]
    return np.sqrt(vel[:, 0] ** 2 + vel[:, 1] ** 2)


def percentile_range(values: np.ndarray, lo: float = 0.5,
                     hi: float = 99.5) -> tuple[float, float]:
    return (float(np.percentile(values, lo)),
            float(np.percentile(values, hi)))


def render_field(data: dict, field: str, out_path: Path, *, title: str,
                 label: str, cmap: str = "viridis", vmin: float | None = None,
                 vmax: float | None = None, xlim: tuple | None = None,
                 ylim: tuple | None = None, figsize: tuple = (8, 6)) -> None:
    """Filled-contour figure of one cell field on the unstructured mesh."""
    pts = data["points"]
    tris, cell_of = build_triangles(data["cells"])
    values = np.asarray(data[field], dtype=float)
    if values.ndim > 1:
        values = np.sqrt((values ** 2).sum(axis=1))
    face = values[cell_of]

    fig, ax = plt.subplots(figsize=figsize)
    tc = ax.tripcolor(pts[:, 0], pts[:, 1], triangles=tris,
                      facecolors=face, cmap=cmap, vmin=vmin, vmax=vmax)
    ax.set_aspect("equal")
    if xlim is not None:
        ax.set_xlim(*xlim)
    if ylim is not None:
        ax.set_ylim(*ylim)
    ax.set_xlabel(r"$x/c$" if "naca" in str(out_path) else "x")
    ax.set_ylabel(r"$y/c$" if "naca" in str(out_path) else "y")
    ax.set_title(title)
    cb = fig.colorbar(tc, ax=ax, shrink=0.85)
    cb.set_label(label)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def make_field_figures(res_dir: Path, figdir: Path) -> list[Path]:
    """Generate the standard field-figure set for one case directory.

    Returns the list of written figure paths.
    """
    case_id = res_dir.name
    vtk = res_dir / "field_final.vtk"
    if not vtk.exists():
        print(f"SKIP {case_id}: no field_final.vtk (run in progress?)")
        return []
    data = parse_vtk(vtk)

    is_cylinder = "cylinder" in case_id
    view = CYLINDER_VIEW if is_cylinder else NACA_VIEW

    written: list[Path] = []
    specs: list[tuple[str, str, str, str, float | None, float | None]] = [
        ("mach", f"{case_id}_mach.png", "Mach number",
         MACH_CMAP, None, None),
    ]
    if "pressure" in data:
        pmin, pmax = percentile_range(data["pressure"])
        specs.append(
            ("pressure", f"{case_id}_pressure.png", "Pressure",
             PRESSURE_CMAP, pmin, pmax))
    if is_cylinder and "velocity" in data:
        vmag = velocity_magnitude(data)
        vmin, vmax = percentile_range(vmag)
        specs.append(
            ("velocity_mag", f"{case_id}_velocity_mag.png",
             "Velocity magnitude", VELOCITY_CMAP, vmin, vmax))
    if "re200" in case_id and "velocity" in data:
        vort = cell_vorticity(data)
        data["vorticity"] = vort
        specs.append(
            ("vorticity", f"{case_id}_vorticity.png", "Vorticity (z)",
             VORTICITY_CMAP, VORTICITY_CLIP[0], VORTICITY_CLIP[1]))

    for field, fname, label, cmap, vmin, vmax in specs:
        if field == "velocity_mag":
            data["velocity_mag"] = velocity_magnitude(data)
            field = "velocity_mag"
        out = figdir / fname
        render_field(data, field, out, title=f"{case_id} — {label}",
                     label=label, cmap=cmap, vmin=vmin, vmax=vmax,
                     xlim=view[0], ylim=view[1])
        print(f"OK {out}")
        written.append(out)
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_dirs", nargs="+", type=Path)
    parser.add_argument("--figdir", type=Path, required=True)
    args = parser.parse_args()

    args.figdir.mkdir(parents=True, exist_ok=True)
    n_ok = 0
    for res in args.result_dirs:
        n_ok += len(make_field_figures(res, args.figdir))
    print(f"wrote {n_ok} field figures to {args.figdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

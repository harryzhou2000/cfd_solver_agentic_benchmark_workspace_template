#!/usr/bin/env python3
"""Render field VTU snapshots to publication-style contour figures.

Usage:
  plot_field.py <field.vtu> <out.png> --var MachNumber [--title "..." ]
                [--xmin X --xmax X --ymin Y --ymax Y] [--vmin V --vmax V]
"""

import argparse
import sys
import xml.etree.ElementTree as ET

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri


def read_vtu(path):
    tree = ET.parse(path)
    piece = tree.getroot().find(".//Piece")
    arrays = {}
    for da in piece.iter("DataArray"):
        name = da.get("Name")
        txt = da.text.split()
        if not txt:
            continue
        arrays[name] = np.array([float(v) for v in txt])
    # Points
    pts = arrays["Points"].reshape(-1, 3)
    x = pts[:, 0]
    y = pts[:, 1]
    conn = arrays["connectivity"].astype(int)
    offsets = arrays["offsets"].astype(int)
    types = arrays["types"].astype(int)
    cells = []
    start = 0
    for off, typ in zip(offsets, types):
        cells.append(conn[start:off])
        start = off
    return x, y, cells, arrays


def cell_triangulation(x, y, cells, values):
    """Expand cells into triangles with duplicated vertices so that each
    triangle carries one cell-centered value (flat-shaded contours)."""
    px, py, pz = [], [], []
    for cell, val in zip(cells, values):
        n = len(cell)
        if n == 3:
            tris = [cell]
        elif n == 4:
            tris = [[cell[0], cell[1], cell[2]], [cell[0], cell[2], cell[3]]]
        else:
            tris = [[cell[0], cell[k], cell[k + 1]] for k in range(1, n - 1)]
        for tri in tris:
            for v in tri:
                px.append(x[v])
                py.append(y[v])
                pz.append(val)
    pts = np.column_stack([px, py])
    nt = len(pz) // 3
    tris = np.arange(3 * nt).reshape(-1, 3)
    tri = mtri.Triangulation(pts[:, 0], pts[:, 1], tris)
    return tri, np.array(pz)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("field")
    ap.add_argument("out")
    ap.add_argument("--var", required=True)
    ap.add_argument("--title", default="")
    ap.add_argument("--xmin", type=float)
    ap.add_argument("--xmax", type=float)
    ap.add_argument("--ymin", type=float)
    ap.add_argument("--ymax", type=float)
    ap.add_argument("--vmin", type=float)
    ap.add_argument("--vmax", type=float)
    ap.add_argument("--levels", type=int, default=40)
    args = ap.parse_args()

    x, y, cells, arrays = read_vtu(args.field)
    if args.var not in arrays:
        print(f"variable {args.var} not in field; have {sorted(arrays)}",
              file=sys.stderr)
        return 1
    values = arrays[args.var]
    tri, zvals = cell_triangulation(x, y, cells, values)

    fig, ax = plt.subplots(figsize=(9, 5.2))
    if args.vmin is not None and args.vmax is not None:
        levels = np.linspace(args.vmin, args.vmax, args.levels)
        cf = ax.tricontourf(tri, zvals, levels=levels, cmap="jet")
    else:
        cf = ax.tricontourf(tri, zvals, levels=args.levels, cmap="jet")
    cb = fig.colorbar(cf, ax=ax)
    cb.set_label(args.var)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    if args.title:
        ax.set_title(args.title)
    if args.xmin is not None:
        ax.set_xlim(args.xmin, args.xmax)
    if args.ymin is not None:
        ax.set_ylim(args.ymin, args.ymax)
    fig.tight_layout()
    fig.savefig(args.out, dpi=170)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Render field contours from a solver field_final.vtu file.

Usage:
  plot_field.py <field.vtu> --var mach --out mach.png [--xlim a b] [--ylim c d]
                 [--vmin x --vmax y] [--title "..."] [--cmap viridis]

Produces a filled contour plot over the actual unstructured cells using
matplotlib triangulation.
"""

import argparse
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


def read_vtu(path):
    """Parse the ASCII VTU files written by the solver."""
    with open(path) as f:
        text = f.read()
    points = re.search(r"<Points>(.*?)</Points>", text, re.S).group(1)
    cells = re.search(r"<Cells>(.*?)</Cells>", text, re.S).group(1)
    cell_data = re.search(r"<CellData>(.*?)</CellData>", text, re.S).group(1)

    def parse_array(block, name=None):
        if name:
            m = re.search(r'<DataArray[^>]*Name="%s"[^>]*>(.*?)</DataArray>' % name, block, re.S)
        else:
            m = re.search(r"<DataArray[^>]*>(.*?)</DataArray>", block, re.S)
        return np.array([float(x) for x in m.group(1).split()])

    pts = parse_array(points).reshape(-1, 3)
    conn = parse_array(cells, "connectivity").astype(int)
    offsets = parse_array(cells, "offsets").astype(int)
    types = parse_array(cells, "types").astype(int)
    ncell = len(offsets)
    tris = []
    quads = []
    start = 0
    for k in range(ncell):
        node_ids = conn[start:offsets[k]]
        if len(node_ids) == 3:
            tris.append(node_ids)
        elif len(node_ids) == 4:
            quads.append(node_ids)
        start = offsets[k]
    tris = np.array(tris, dtype=int) if tris else np.zeros((0, 3), dtype=int)
    quads = np.array(quads, dtype=int) if quads else np.zeros((0, 4), dtype=int)
    data = {}
    for m in re.finditer(r'<DataArray[^>]*Name="([^"]+)"[^>]*>(.*?)</DataArray>', cell_data, re.S):
        data[m.group(1)] = np.array([float(x) for x in m.group(2).split()])
    return pts[:, :2], tris, quads, data


def centroid_dual(pts, tris, quads):
    """Build a triangulation whose vertices are cell centroids, suitable for
    smooth contours of cell-centered data. For each original mesh vertex we
    fan-triangulate the polygon of incident cell centroids (ordered by
    angle around the vertex)."""
    cells = [list(t) for t in tris] + [list(q) for q in quads]
    ncell = len(cells)
    centroids = np.zeros((ncell, 2))
    for k, c in enumerate(cells):
        centroids[k] = pts[c].mean(axis=0)
    incident = {}
    for k, c in enumerate(cells):
        for v in c:
            incident.setdefault(int(v), []).append(k)
    triangles = []
    for v, clist in incident.items():
        if len(clist) < 3:
            continue
        ang = np.arctan2(centroids[clist, 1] - pts[v, 1],
                         centroids[clist, 0] - pts[v, 0])
        order = np.argsort(ang)
        cl = [clist[i] for i in order]
        for i in range(len(cl) - 2):
            triangles.append((cl[0], cl[i + 1], cl[i + 2]))
    if not triangles:
        # Fallback: cell-corner triangulation (values at vertices).
        return None
    return mtri.Triangulation(centroids[:, 0], centroids[:, 1],
                              np.array(triangles, dtype=int))


def cell_triangulation(pts, tris, quads):
    """Triangulation of the mesh cells (quads split into two triangles) with
    one value per triangle (flat shading)."""
    q2 = np.vstack([quads[:, [0, 1, 2]], quads[:, [0, 2, 3]]]) if quads.size else \
        np.zeros((0, 3), dtype=int)
    all_t = np.vstack([tris, q2]) if tris.size else q2
    return mtri.Triangulation(pts[:, 0], pts[:, 1], all_t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("field")
    ap.add_argument("--var", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--xlim", nargs=2, type=float)
    ap.add_argument("--ylim", nargs=2, type=float)
    ap.add_argument("--vmin", type=float)
    ap.add_argument("--vmax", type=float)
    ap.add_argument("--title", default="")
    ap.add_argument("--cmap", default="viridis")
    ap.add_argument("--levels", type=int, default=60)
    ap.add_argument("--wake", action="store_true", help="use wake-style color mapping")
    ap.add_argument("--smooth", action="store_true",
                    help="smooth contouring via centroid dual (may be slow)")
    args = ap.parse_args()

    pts, tris, quads, data = read_vtu(args.field)
    if args.var not in data:
        sys.exit(f"variable {args.var} not found; have {sorted(data)}")
    values = data[args.var]

    if args.smooth:
        tri = centroid_dual(pts, tris, quads)
        if tri is None:
            sys.exit("smooth mode failed; use flat shading")
    else:
        tri = cell_triangulation(pts, tris, quads)

    fig, ax = plt.subplots(figsize=(9, 6), dpi=150)
    vmin = args.vmin if args.vmin is not None else np.percentile(values, 1)
    vmax = args.vmax if args.vmax is not None else np.percentile(values, 99)
    if args.smooth:
        cf = ax.tricontourf(tri, values, levels=np.linspace(vmin, vmax, args.levels),
                            cmap=args.cmap)
    else:
        # Cell-centered values: flat per-triangle shading (quads contribute
        # two triangles carrying the same cell value).
        if quads.size:
            ntri = len(tris)
            z = np.concatenate([values[:ntri], np.repeat(values[ntri:], 2)])
        else:
            z = values
        cf = ax.tripcolor(tri, z, cmap=args.cmap, vmin=vmin, vmax=vmax,
                          shading="flat")
        args.levels = 0
    cb = fig.colorbar(cf, ax=ax, label=args.var)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    if args.xlim:
        ax.set_xlim(*args.xlim)
    if args.ylim:
        ax.set_ylim(*args.ylim)
    ax.set_aspect("equal")
    ax.set_title(args.title or args.var)
    fig.tight_layout()
    fig.savefig(args.out, dpi=150)
    print(f"wrote {args.out} ({args.var}: {vmin:.4g}..{vmax:.4g})")


if __name__ == "__main__":
    main()

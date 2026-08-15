#!/usr/bin/env python3
"""Quick VTU ASCII reader + field plot helper (debugging and figures)."""
import argparse
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_vtu(path):
    """Parse an ASCII XML VTU (single piece, as written by cfd_solver)."""
    with open(path) as f:
        text = f.read()
    import re
    def arr(name, typ=int):
        if name == "Points":
            m = re.search(r'<Points>\s*<DataArray[^>]*>\s*(.*?)\s*</DataArray>\s*</Points>', text, re.S)
        else:
            m = re.search(r'<DataArray[^>]*Name="%s"[^>]*>\s*(.*?)\s*</DataArray>' % name, text, re.S)
        if not m:
            return None
        return np.array([typ(x) for x in m.group(1).split()])
    pts = arr("Points", float)
    conn = arr("connectivity", int)
    offs = arr("offsets", int)
    types = arr("types", int)
    npts = len(pts) // 3
    points = pts.reshape(npts, 3)
    cells = []
    start = 0
    for o in offs:
        cells.append(conn[start:o])
        start = o
    data = {}
    for name in ["Density", "Velocity", "Pressure", "Mach", "Temperature",
                 "TotalEnergy", "RankId", "GlobalCellId"]:
        d = arr(name, float if name != "RankId" and name != "GlobalCellId" else int)
        if d is not None:
            data[name] = d
    return points, cells, data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("vtu")
    ap.add_argument("--out", default="/tmp/field.png")
    ap.add_argument("--var", default="Mach")
    ap.add_argument("--xlim", type=float, nargs=2, default=None)
    ap.add_argument("--ylim", type=float, nargs=2, default=None)
    ap.add_argument("--vmin", type=float, default=None)
    ap.add_argument("--vmax", type=float, default=None)
    args = ap.parse_args()

    points, cells, data = read_vtu(args.vtu)
    var = data[args.var]
    from matplotlib.collections import PolyCollection
    polys = [points[c][:, :2] for c in cells]
    pc = PolyCollection(polys, array=var, edgecolors="none", cmap="jet")
    fig, ax = plt.subplots(figsize=(9, 6))
    ax.add_collection(pc)
    if args.vmin is not None or args.vmax is not None:
        pc.set_clim(args.vmin, args.vmax)
    ax.autoscale()
    if args.xlim: ax.set_xlim(*args.xlim)
    if args.ylim: ax.set_ylim(*args.ylim)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    cb = fig.colorbar(pc, ax=ax)
    cb.set_label(args.var)
    fig.savefig(args.out, dpi=150, bbox_inches="tight")
    print(f"wrote {args.out}: {len(cells)} cells, range {var.min():.4g}..{var.max():.4g}")


if __name__ == "__main__":
    main()

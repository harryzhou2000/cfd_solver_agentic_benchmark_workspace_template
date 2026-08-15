#!/usr/bin/env python3
"""Filled-cell contour plot from a cfd_solver VTU, rendered with stdlib only.

Usage: plot_field.py <out.png> <field.vtu> <scalar> [--xmin A --xmax B
       --ymin C --ymax D] [--vmin A --vmax B] [--title T] [--label L]
       [--cmap name] [--percentiles lo,hi] [--cell-value]
"""

import math
import sys

from plot_canvas import Canvas
from vtu_reader import read_vtu, cell_centroids


def cmap_viridis(t):
    # Simplified viridis-like interpolation stops.
    stops = [(68, 1, 84), (72, 40, 120), (62, 74, 137), (49, 104, 142),
             (38, 130, 142), (31, 158, 137), (53, 183, 121), (109, 205, 89),
             (180, 222, 44), (253, 231, 37)]
    t = max(0.0, min(1.0, t)) * (len(stops) - 1)
    i = int(t)
    f = t - i
    a = stops[min(i, len(stops) - 1)]
    b = stops[min(i + 1, len(stops) - 1)]
    return tuple(int(a[k] + f * (b[k] - a[k])) for k in range(3))


def cmap_coolwarm(t):
    t = max(0.0, min(1.0, t))
    # blue -> white -> red
    if t < 0.5:
        f = t * 2
        return (int(59 + (255 - 59) * f), int(76 + (255 - 76) * f),
                int(192 + (255 - 192) * f))
    f = (t - 0.5) * 2
    return (int(255 - (255 - 178) * f), int(255 - (255 - 24) * f),
            int(255 - (255 - 43) * f))


def cmap_jet(t):
    t = max(0.0, min(1.0, t))
    x = t * 7
    if x < 1: return (0, 0, int(255 * x))
    if x < 3: return (0, int(255 * (x - 1)), 255)
    if x < 4: return (int(255 * (x - 3)), 255, int(255 * (4 - x)))
    if x < 6: return (255, int(255 * (6 - x)), 0)
    return (int(255 * (7 - x)), 0, 0)


CMAPS = {"viridis": cmap_viridis, "coolwarm": cmap_coolwarm, "jet": cmap_jet}


def fmt(v):
    a = abs(v)
    if a >= 1e5 or a < 1e-3:
        return f"{v:.1e}"
    if a >= 100:
        return f"{v:.0f}"
    if a >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) < 3:
        print(__doc__)
        return 1
    out, path, scalar = args[0], args[1], args[2]
    opts = {}
    rest = args[3:]
    i = 0
    while i < len(rest):
        k = rest[i]
        v = rest[i + 1] if i + 1 < len(rest) else ""
        opts[k[2:]] = v
        i += 2
    vtu = read_vtu(path)
    if scalar not in vtu["cell_data"]:
        print(f"scalar {scalar} not in {sorted(vtu['cell_data'])}")
        return 1
    vals = vtu["cell_data"][scalar]
    if isinstance(vals[0], tuple):
        vals = [math.hypot(v[0], v[1]) for v in vals]
    cen = cell_centroids(vtu)
    x0 = float(opts.get("xmin", -5.0)); x1 = float(opts.get("xmax", 5.0))
    y0 = float(opts.get("ymin", -5.0)); y1 = float(opts.get("ymax", 5.0))
    vmin = float(opts.get("vmin", "nan"))
    vmax = float(opts.get("vmax", "nan"))
    pcts = opts.get("percentiles", "")
    if math.isnan(vmin) or math.isnan(vmax):
        finite = [v for v in vals if math.isfinite(v)]
        if pcts:
            lo, hi = (float(t) for t in pcts.split(","))
            sv = sorted(finite)
            vmin = sv[int(lo / 100 * (len(sv) - 1))]
            vmax = sv[int(hi / 100 * (len(sv) - 1))]
        else:
            vmin, vmax = min(finite), max(finite)
    if vmax <= vmin:
        vmax = vmin + 1e-12
    cmap = CMAPS.get(opts.get("cmap", "viridis"))
    W, H = 1100, 800
    canvas = Canvas(W, H)
    # plot area with 30px margin, square-ish mapping
    ax0, ay0, ax1, ay1 = 70, 60, 1010, 730
    sx = (ax1 - ax0) / (x1 - x0)
    sy = (ay1 - ay0) / (y1 - y0)
    for ci in range(len(vtu["cells"])):
        xc, yc = cen[ci]
        if xc < x0 or xc > x1 or yc < y0 or yc > y1:
            continue
        v = vals[ci]
        if not math.isfinite(v):
            continue
        t = (v - vmin) / (vmax - vmin)
        r, g, b = cmap(t)
        color = (r, g, b, 255)
        pts = []
        for nid in vtu["cells"][ci]:
            px = ax0 + (vtu["points"][nid][0] - x0) * sx
            py = ay1 - (vtu["points"][nid][1] - y0) * sy
            pts.append((px, py))
        canvas.fill_polygon(pts, color)
    # axes frame
    canvas.rect(ax0, ay0, ax1, ay1, (30, 30, 30, 255), 2)
    xticks = []
    step = (x1 - x0) / 6
    mag = 10 ** math.floor(math.log10(step))
    step = math.ceil(step / mag) * mag
    t = math.ceil(x0 / step) * step
    while t <= x1 + 1e-9:
        xticks.append(t); t += step
    for t in xticks:
        px = ax0 + (t - x0) * sx
        canvas.line(px, ay1, px, ay1 + 5, (30, 30, 30, 255), 1)
        canvas.text(px - 18, ay1 + 8, fmt(t), (0, 0, 0, 255), 2)
    yticks = []
    step = (y1 - y0) / 6
    mag = 10 ** math.floor(math.log10(step))
    step = math.ceil(step / mag) * mag
    t = math.ceil(y0 / step) * step
    while t <= y1 + 1e-9:
        yticks.append(t); t += step
    for t in yticks:
        py = ay1 - (t - y0) * sy
        canvas.line(ax0 - 5, py, ax0, py, (30, 30, 30, 255), 1)
        canvas.text(ax0 - 54, py - 6, fmt(t), (0, 0, 0, 255), 2)
    canvas.text_center((ax0 + ax1) / 2, ay1 + 30, "x", (0, 0, 0, 255), 2)
    cx = ax0 - 70
    yy = (ay0 + ay1) / 2 - 14
    for ch in "y":
        canvas.text(cx, yy, ch, (0, 0, 0, 255), 2)
        yy += 14
    title = opts.get("title", f"{scalar}")
    canvas.text_center(W / 2, 14, title, (0, 0, 0, 255), 3)
    # colorbar
    cbx0, cby0, cbx1, cby1 = 1040, 100, 1060, 690
    nsteps = 128
    for k in range(nsteps):
        t = k / (nsteps - 1)
        r, g, b = cmap(t)
        canvas.fill(cbx0, cby1 - (k + 1) * (cby1 - cby0) / nsteps,
                    cbx1, cby1 - k * (cby1 - cby0) / nsteps, (r, g, b, 255))
    canvas.rect(cbx0, cby0, cbx1, cby1, (30, 30, 30, 255), 1)
    canvas.text(cbx1 + 8, cby1 - 8, fmt(vmax), (0, 0, 0, 255), 2)
    canvas.text(cbx1 + 8, cby0 + 4, fmt(vmin), (0, 0, 0, 255), 2)
    label = opts.get("label", scalar)
    canvas.text_center((cbx0 + cbx1) / 2, cby0 - 24, label, (0, 0, 0, 255), 2)
    canvas.save(out)
    print(f"wrote {out} (vmin={vmin:.6g} vmax={vmax:.6g})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

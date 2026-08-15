#!/usr/bin/env python3
"""Line plots (residual/force histories) rendered to PNG with the stdlib canvas.

Usage: plot_lines.py <out.png> <csv> <xcol> <ycol...> [--title T] [--xlabel X]
       [--ylabel Y] [--logy] [--legend LABELS] [--ymin A --ymax B]
"""

import csv
import math
import sys

from plot_canvas import Canvas


BLACK = (20, 20, 20, 255)
GRID = (210, 210, 210, 255)
AXIS = (60, 60, 60, 255)
COLORS = [(31, 119, 180, 255), (214, 39, 40, 255), (44, 160, 44, 255),
          (148, 103, 189, 255), (255, 127, 14, 255), (23, 190, 207, 255),
          (227, 119, 194, 255), (188, 189, 34, 255)]


def nice_ticks(lo, hi, target=8):
    if lo == hi:
        lo -= 0.5; hi += 0.5
    span = hi - lo
    step = span / target
    mag = 10 ** math.floor(math.log10(step))
    for m in (1, 2, 2.5, 5, 10):
        if m * mag >= step:
            step = m * mag
            break
    t0 = math.ceil(lo / step) * step
    ticks = []
    t = t0
    while t <= hi + 1e-9:
        ticks.append(t)
        t += step
    return ticks


def fmt(v):
    if v == 0:
        return "0"
    a = abs(v)
    if a >= 1e5 or a < 1e-3:
        return f"{v:.1e}"
    if a >= 100:
        return f"{v:.0f}"
    if a >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def draw_axes(canvas, x0, y0, x1, y1, xticks, yticks, xlabel, ylabel,
              xtick_fmt, ytick_fmt, logy):
    canvas.line(x0, y0, x1, y0, AXIS, 2)
    canvas.line(x0, y0, x0, y1, AXIS, 2)
    for t in xticks:
        px = x0 + (t - xticks[0]) / (xticks[-1] - xticks[0]) * (x1 - x0)
        canvas.line(px, y0, px, y0 + 4, AXIS, 1)
        canvas.text(px - 20, y0 + 6, xtick_fmt(t), BLACK, 2)
    for t in yticks:
        py = y0 + (1 - (t - yticks[0]) / (yticks[-1] - yticks[0])) * (y1 - y0)
        canvas.line(x0 - 4, py, x0, py, AXIS, 1)
        canvas.text(x0 - 54, py - 6, ytick_fmt(t), BLACK, 2)
    canvas.text_center((x0 + x1) / 2, y1 + 14, xlabel, BLACK, 2)
    # y label vertical: draw rotated by stacking characters
    if ylabel:
        cx = x0 - 82
        yy = (y0 + y1) / 2 - len(ylabel) * 7
        for ch in ylabel:
            canvas.text(cx, yy, ch, BLACK, 2)
            yy += 14


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) < 4:
        print(__doc__)
        return 1
    out = args[0]
    csvpath = args[1]
    xcol = args[2]
    ycols = args[3].split(",")
    opts = {}
    rest = args[4:]
    i = 0
    while i < len(rest):
        k = rest[i]; v = rest[i + 1] if i + 1 < len(rest) else ""
        opts[k[2:]] = v
        i += 2
    with open(csvpath, newline="") as f:
        rows = list(csv.DictReader(f))
    xvals = [float(r[xcol]) for r in rows]
    series = []
    for yc in ycols:
        series.append([float(r[yc]) for r in rows])
    title = opts.get("title", "")
    xlabel = opts.get("xlabel", xcol)
    ylabel = opts.get("ylabel", ",".join(ycols))
    logy = "logy" in opts
    legend = opts.get("legend", ",".join(ycols)).split(",")
    W, H = 1100, 760
    canvas = Canvas(W, H)
    x0, y0, x1, y1 = 150, 640, 1020, 90
    lo, hi = min(xvals), max(xvals)
    if lo == hi:
        lo -= 1; hi += 1
    xticks = nice_ticks(lo, hi, 8)
    ylo, yhi = min(min(s) for s in series), max(max(s) for s in series)
    if logy:
        pos = [v for s in series for v in s if v > 0]
        if not pos:
            pos = [1e-12]
        ylo = 10 ** math.floor(math.log10(min(pos)))
        yhi = 10 ** math.ceil(math.log10(max(pos)))
        yticks = [10 ** k for k in range(int(round(math.log10(ylo))),
                                         int(round(math.log10(yhi))) + 1)]
    else:
        pad = 0.05 * (yhi - ylo) if yhi > ylo else 0.5
        ylo -= pad; yhi += pad
        yticks = nice_ticks(ylo, yhi, 8)
    # grid + series
    for t in xticks:
        px = x0 + (t - xticks[0]) / (xticks[-1] - xticks[0]) * (x1 - x0)
        canvas.line(px, y0, px, y1, GRID, 1)
    for t in yticks:
        py = y0 + (1 - (t - yticks[0]) / (yticks[-1] - yticks[0])) * (y1 - y0)
        canvas.line(x0, py, x1, py, GRID, 1)
    for si, sv in enumerate(series):
        pts = []
        for xv, yv in zip(xvals, sv):
            if logy and yv <= 0:
                continue
            px = x0 + (xv - xticks[0]) / (xticks[-1] - xticks[0]) * (x1 - x0)
            py = y0 + (1 - (math.log10(yv) if logy else yv - yticks[0]) /
                       ((math.log10(yticks[-1]) if logy else yticks[-1]) -
                        (math.log10(yticks[0]) if logy else yticks[0]))) * (y1 - y0)
            pts.append((px, py))
        canvas.polyline(pts, COLORS[si % len(COLORS)], 2)
    draw_axes(canvas, x0, y0, x1, y1, xticks, yticks, xlabel, ylabel,
              lambda t: fmt(t), (lambda t: fmt(t)), logy)
    if title:
        canvas.text_center(W / 2, 20, title, BLACK, 3)
    if legend:
        lx = x1 - 280
        ly = y1 + 12
        for si, lab in enumerate(legend):
            canvas.line(lx, ly + 6, lx + 26, ly + 6, COLORS[si % len(COLORS)], 3)
            canvas.text(lx + 32, ly, lab, BLACK, 2)
            ly += 18
    canvas.save(out)
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

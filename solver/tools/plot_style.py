"""Shared publication-style matplotlib settings and field-plot helpers."""

from __future__ import annotations

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
from mpl_toolkits.axes_grid1 import make_axes_locatable
import numpy as np

RC = {
    "figure.dpi": 160,
    "savefig.dpi": 160,
    "savefig.bbox": "tight",
    "font.size": 11,
    "font.family": "serif",
    "mathtext.fontset": "dejavuserif",
    "axes.titlesize": 12,
    "axes.labelsize": 12,
    "axes.linewidth": 0.9,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "grid.linewidth": 0.6,
    "legend.fontsize": 10,
    "legend.frameon": True,
    "legend.framealpha": 0.9,
    "lines.linewidth": 1.6,
    "xtick.direction": "in",
    "ytick.direction": "in",
    "xtick.top": True,
    "ytick.right": True,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
}

# Colour-blind-safe qualitative palette for line plots.
SERIES = ["#0072B2", "#D55E00", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#000000"]


def apply_style() -> None:
    plt.rcParams.update(RC)


def build_triangulation(mesh, window=None):
    """Triangulate the unstructured mesh, optionally masking a view window."""
    tris, src = mesh.triangulate()
    x = mesh.points[:, 0]
    y = mesh.points[:, 1]
    tri = mtri.Triangulation(x, y, tris)
    if window is not None:
        xmin, xmax, ymin, ymax = window
        cx = x[tris].mean(axis=1)
        cy = y[tris].mean(axis=1)
        pad_x = 0.08 * (xmax - xmin)
        pad_y = 0.08 * (ymax - ymin)
        mask = ~((cx > xmin - pad_x) & (cx < xmax + pad_x) &
                 (cy > ymin - pad_y) & (cy < ymax + pad_y))
        tri.set_mask(mask)
    return tri, src


def field_contour(ax, mesh, values, window=None, levels=60, cmap="viridis",
                  vmin=None, vmax=None, clip_percentile=None, lines=0,
                  line_color="k", line_width=0.35):
    """Filled contours of a cell-centred field on the actual mesh triangulation.

    Cell values are averaged onto the mesh nodes with area weights, so the plot
    uses the real unstructured cells rather than an interpolation grid.
    """
    tri, _ = build_triangulation(mesh, window)
    nodal = mesh.cell_to_point(values)
    if window is not None:
        sel = ((mesh.points[:, 0] > window[0]) & (mesh.points[:, 0] < window[1]) &
               (mesh.points[:, 1] > window[2]) & (mesh.points[:, 1] < window[3]))
        sample = nodal[sel] if sel.any() else nodal
    else:
        sample = nodal
    if vmin is None or vmax is None:
        if clip_percentile is not None:
            lo, hi = np.percentile(sample, clip_percentile)
        else:
            lo, hi = float(np.min(sample)), float(np.max(sample))
        vmin = lo if vmin is None else vmin
        vmax = hi if vmax is None else vmax
    if vmax <= vmin:
        vmax = vmin + 1.0e-12
    lv = np.linspace(vmin, vmax, levels)
    cf = ax.tricontourf(tri, np.clip(nodal, vmin, vmax), levels=lv, cmap=cmap, extend="both")
    # Remove the white seams between filled contour bands (API differs across
    # Matplotlib versions).
    try:
        cf.set_edgecolor("face")
    except AttributeError:
        for c in getattr(cf, "collections", []):
            c.set_edgecolor("face")
    if lines:
        ax.tricontour(tri, np.clip(nodal, vmin, vmax),
                      levels=np.linspace(vmin, vmax, lines), colors=line_color,
                      linewidths=line_width, alpha=0.6)
    if window is not None:
        ax.set_xlim(window[0], window[1])
        ax.set_ylim(window[2], window[3])
    ax.set_aspect("equal")
    return cf


def draw_body(ax, surface_xy, color="k", lw=1.2):
    """Overlay the wall outline from surface.csv so the body is unambiguous."""
    if surface_xy is None or len(surface_xy) == 0:
        return
    x, y = surface_xy[:, 0], surface_xy[:, 1]
    ax.plot(np.append(x, x[0]), np.append(y, y[0]), color=color, lw=lw, zorder=5)


def add_colorbar(fig, mappable, ax, label):
    """Colour bar tied to the height of the (equal-aspect) axes.

    ``fig.colorbar(..., ax=ax)`` sizes the bar from the figure, which leaves it
    much taller than an equal-aspect field plot; ``make_axes_locatable`` ties it
    to the axes instead.
    """
    divider = make_axes_locatable(ax)
    cax = divider.append_axes("right", size="3.5%", pad=0.12)
    cb = fig.colorbar(mappable, cax=cax)
    cb.set_label(label)
    cb.ax.tick_params(labelsize=9)
    return cb

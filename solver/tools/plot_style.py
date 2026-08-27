"""Shared publication-quality matplotlib styling for the CFD report figures.

Importing this module does not change global state; call apply_style() (or use
any of the figure helpers, which call it for you) to install the rcParams.  The
intent is that every figure in the report looks like it came from the same
paper: consistent fonts, line widths, grids, colour maps and save resolution.

Conventions chosen here (documented so the report can state them):

*   Sequential, perceptually uniform colour maps for positive-definite or
    monotone fields (Mach number, pressure, velocity magnitude, temperature).
*   A diverging, zero-centred colour map for signed fields (vorticity), always
    with symmetric colour limits so that the neutral colour means zero.
*   Colour limits from the 1st-99th percentile of the data by default, so that a
    handful of outliers (stagnation points, shock over/undershoots) cannot
    collapse the useful range.  Whenever the limits clip the data the caller is
    expected to say so on the figure; format_clip_note() builds that text.
*   Saved at 200 dpi with a tight bounding box.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional, Sequence, Tuple

import matplotlib

matplotlib.use("Agg")  # headless: there is no display in the benchmark container

import matplotlib.pyplot as plt
import numpy as np

__all__ = [
    "apply_style",
    "SAVE_DPI",
    "FIELD_COLORMAPS",
    "colormap_for",
    "is_diverging",
    "percentile_limits",
    "symmetric_limits",
    "format_clip_note",
    "new_figure",
    "new_field_figure",
    "label_axes",
    "finalize_line_axes",
    "add_case_title",
    "add_colorbar",
    "annotate_corner",
    "set_view_window",
    "save_figure",
]

#: Resolution used by save_figure().
SAVE_DPI = 200

#: Base font size in points; the other sizes are derived from it.
_BASE_FONT = 12

#: Field name (lowercase substring) -> colour map.  Sequential maps for
#: monotone quantities, diverging for signed ones.
FIELD_COLORMAPS = {
    "mach": "viridis",
    "pressure": "inferno",
    "density": "cividis",
    "velocity": "viridis",
    "temperature": "magma",
    "total_energy": "magma",
    "vorticity": "RdBu_r",
    "rank": "tab20",
}

#: Fields that are signed and therefore need symmetric, zero-centred limits.
_DIVERGING_FIELDS = ("vorticity", "vort", "omega")


def apply_style() -> None:
    """Install the shared rcParams.  Safe to call repeatedly."""
    plt.rcParams.update(
        {
            # Fonts: a stack that is guaranteed to exist in a minimal container.
            "font.family": "sans-serif",
            "font.sans-serif": ["DejaVu Sans"],
            "font.size": _BASE_FONT,
            "axes.titlesize": _BASE_FONT + 1,
            "axes.labelsize": _BASE_FONT,
            "xtick.labelsize": _BASE_FONT - 1,
            "ytick.labelsize": _BASE_FONT - 1,
            "legend.fontsize": _BASE_FONT - 1,
            "figure.titlesize": _BASE_FONT + 1,
            # Lines and markers.
            "lines.linewidth": 1.6,
            "lines.markersize": 4.0,
            "lines.markeredgewidth": 0.9,
            # Axes furniture.
            "axes.linewidth": 0.9,
            "axes.grid": True,
            "axes.axisbelow": True,
            "grid.alpha": 0.3,
            "grid.linewidth": 0.6,
            "grid.linestyle": "-",
            "axes.prop_cycle": plt.cycler(
                color=[
                    "#0072B2",
                    "#D55E00",
                    "#009E73",
                    "#CC79A7",
                    "#56B4E9",
                    "#E69F00",
                    "#333333",
                ]
            ),
            # Ticks: inward on all four sides, publication style.
            "xtick.direction": "in",
            "ytick.direction": "in",
            "xtick.top": True,
            "ytick.right": True,
            "xtick.major.size": 4.5,
            "ytick.major.size": 4.5,
            "xtick.minor.size": 2.5,
            "ytick.minor.size": 2.5,
            "xtick.major.width": 0.9,
            "ytick.major.width": 0.9,
            # Legend.
            "legend.frameon": True,
            "legend.framealpha": 0.9,
            "legend.edgecolor": "0.7",
            "legend.borderpad": 0.4,
            # Output.
            "figure.dpi": 110,
            "savefig.dpi": SAVE_DPI,
            "savefig.bbox": "tight",
            "savefig.pad_inches": 0.05,
            "figure.autolayout": False,
            "image.cmap": "viridis",
        }
    )


def colormap_for(variable: str) -> str:
    """Return the agreed colour map for a field name.

    Matching is on lowercase substrings so that 'velocity_magnitude',
    'mach_number' and 'omega_z' all resolve sensibly.  Unknown fields fall back
    to viridis.
    """
    key = (variable or "").lower()
    if is_diverging(key):
        return FIELD_COLORMAPS["vorticity"]
    for name, cmap in FIELD_COLORMAPS.items():
        if name in key:
            return cmap
    return "viridis"


def is_diverging(variable: str) -> bool:
    """True when a field is signed and should get symmetric colour limits."""
    key = (variable or "").lower()
    return any(name in key for name in _DIVERGING_FIELDS)


def percentile_limits(
    values: np.ndarray,
    low: float = 1.0,
    high: float = 99.0,
    pad_fraction: float = 0.0,
) -> Tuple[float, float]:
    """Robust colour limits from percentiles of the finite data.

    Returns (vmin, vmax).  Near-constant data is widened slightly so that
    contouring still produces visible levels instead of failing on a zero-width
    range.
    """
    finite = np.asarray(values, dtype=np.float64).ravel()
    finite = finite[np.isfinite(finite)]
    if finite.size == 0:
        return (0.0, 1.0)
    vmin = float(np.percentile(finite, low))
    vmax = float(np.percentile(finite, high))
    if not np.isfinite(vmin) or not np.isfinite(vmax) or vmax <= vmin:
        vmin, vmax = float(finite.min()), float(finite.max())
    if vmax <= vmin:
        centre = 0.5 * (vmin + vmax)
        spread = max(abs(centre) * 1.0e-3, 1.0e-9)
        vmin, vmax = centre - spread, centre + spread
    if pad_fraction:
        pad = pad_fraction * (vmax - vmin)
        vmin, vmax = vmin - pad, vmax + pad
    return (vmin, vmax)


def symmetric_limits(
    values: np.ndarray, clip: Optional[float] = None, percentile: float = 99.0
) -> Tuple[float, float]:
    """Zero-centred limits (-m, +m) for a signed field.

    When 'clip' is given it is used directly; otherwise m is the requested
    percentile of the absolute value.
    """
    if clip is not None and clip > 0:
        return (-float(clip), float(clip))
    finite = np.asarray(values, dtype=np.float64).ravel()
    finite = finite[np.isfinite(finite)]
    if finite.size == 0:
        return (-1.0, 1.0)
    m = float(np.percentile(np.abs(finite), percentile))
    if not np.isfinite(m) or m <= 0.0:
        m = float(np.max(np.abs(finite)))
    if not np.isfinite(m) or m <= 0.0:
        m = 1.0
    return (-m, m)


def format_clip_note(values: np.ndarray, vmin: float, vmax: float) -> str:
    """Return a short note stating the colour range when it clips the data.

    Returns an empty string when nothing is clipped, so callers can append it
    unconditionally.
    """
    finite = np.asarray(values, dtype=np.float64).ravel()
    finite = finite[np.isfinite(finite)]
    if finite.size == 0:
        return ""
    data_min, data_max = float(finite.min()), float(finite.max())
    clipped_low = data_min < vmin - 1.0e-12 * max(1.0, abs(vmin))
    clipped_high = data_max > vmax + 1.0e-12 * max(1.0, abs(vmax))
    if not (clipped_low or clipped_high):
        return ""
    return "colour range clipped to [%.3g, %.3g]; data span [%.3g, %.3g]" % (
        vmin,
        vmax,
        data_min,
        data_max,
    )


def new_figure(figsize=(6.6, 4.4)):
    """Create a styled single-axes figure for line plots."""
    apply_style()
    fig, ax = plt.subplots(figsize=figsize)
    return fig, ax


def new_field_figure(figsize=(8.2, 4.6)):
    """Create a styled single-axes figure for a field contour plot.

    The axes use an equal aspect ratio (essential for reading a flow field) and
    the background grid is switched off so the contours stay legible.
    """
    apply_style()
    fig, ax = plt.subplots(figsize=figsize)
    ax.set_aspect("equal", adjustable="box")
    ax.grid(False)
    return fig, ax


def label_axes(ax, xlabel: str, ylabel: str, title: Optional[str] = None) -> None:
    """Set the axis labels, and optionally the title, in one call."""
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if title:
        ax.set_title(title)


def finalize_line_axes(
    ax, legend: bool = True, legend_loc: str = "best", minor_ticks: bool = True
) -> None:
    """Apply the shared finishing touches to a line-plot axes.

    Adds a legend when labelled artists exist, enables minor ticks and makes
    sure the grid is drawn beneath the data.
    """
    if minor_ticks:
        ax.minorticks_on()
        ax.tick_params(which="minor", top=True, right=True)
    ax.grid(True, which="major", alpha=0.3)
    ax.grid(True, which="minor", alpha=0.15, linewidth=0.4)
    if legend:
        handles, _labels = ax.get_legend_handles_labels()
        if handles:
            ax.legend(loc=legend_loc)


def add_case_title(ax, case_id: str, description: str) -> None:
    """Title an axes with the case id plus a description of the quantity."""
    ax.set_title("%s  |  %s" % (case_id, description))


def add_colorbar(fig, mappable, ax, label: str, extend: str = "neither"):
    """Attach a labelled colour bar sized to match the axes."""
    cbar = fig.colorbar(mappable, ax=ax, pad=0.02, fraction=0.046, extend=extend)
    cbar.set_label(label)
    cbar.ax.tick_params(labelsize=_BASE_FONT - 1)
    return cbar


def annotate_corner(
    ax, text: str, loc: str = "lower left", fontsize: Optional[float] = None
) -> None:
    """Place a small boxed annotation in a corner of the axes.

    Used to state clipped colour ranges and analysis windows directly on the
    figure, as the visualisation-quality requirements ask for.
    """
    if not text:
        return
    positions = {
        "lower left": (0.015, 0.02, "left", "bottom"),
        "lower right": (0.985, 0.02, "right", "bottom"),
        "upper left": (0.015, 0.98, "left", "top"),
        "upper right": (0.985, 0.98, "right", "top"),
    }
    x, y, ha, va = positions.get(loc, positions["lower left"])
    ax.text(
        x,
        y,
        text,
        transform=ax.transAxes,
        ha=ha,
        va=va,
        fontsize=fontsize if fontsize is not None else _BASE_FONT - 3,
        bbox=dict(
            boxstyle="round,pad=0.28",
            facecolor="white",
            alpha=0.82,
            edgecolor="0.7",
            linewidth=0.6,
        ),
    )


def set_view_window(ax, window: Optional[Sequence[float]]) -> None:
    """Apply an (xmin, xmax, ymin, ymax) view window when one is given."""
    if window is None:
        return
    xmin, xmax, ymin, ymax = (float(v) for v in window)
    ax.set_xlim(xmin, xmax)
    ax.set_ylim(ymin, ymax)


def save_figure(fig, path, dpi: int = SAVE_DPI, close: bool = True) -> Path:
    """Save a figure at report resolution, creating parent directories.

    Returns the path written so callers can record it in the figure manifest.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=dpi, bbox_inches="tight")
    if close:
        plt.close(fig)
    return path

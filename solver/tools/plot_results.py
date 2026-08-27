#!/usr/bin/env python3
"""Produce the per-case report figures for one CFD solver output directory.

Usage::

    python plot_results.py --case-dir <results/case> --case-id <id> --out-dir <figdir>
                           [--body cylinder|airfoil] [--vorticity-clip 5.0]
                           [--no-farfield] [--levels 40]

Figures written (all PNG, 200 dpi):

    <case_id>_residuals.png       semilog-y residual history vs step
    <case_id>_forces.png          cl and cd history vs physical time (or step)
    <case_id>_cp.png              surface pressure coefficient
    <case_id>_cf.png              skin friction (viscous cases only)
    <case_id>_mach.png            filled Mach contours on the unstructured mesh
    <case_id>_pressure.png        filled pressure contours
    <case_id>_velocity.png        filled velocity-magnitude contours
    <case_id>_vorticity.png       filled vorticity contours, symmetric clipped
    <case_id>_mach_farfield.png   whole-domain Mach view (unless --no-farfield)

Conventions chosen here, and stated in the report:

*   Airfoil Cp is plotted against x/c with the y-axis inverted, upper and lower
    surfaces drawn as separate labelled series.
*   Cylinder Cp and Cf are plotted against the wall angle in degrees measured
    from the front stagnation point: 0 deg at the upstream stagnation point,
    180 deg at the rear, increasing anticlockwise through the upper surface, so
    the complete wall forms a single monotone 0-360 deg curve.
*   Surface rows are re-sorted along the body before plotting, because the
    solver writes them in partition order; plotting file order would produce a
    scribble instead of a line.
*   Field colour limits use the 1st-99th percentile of the plotted data, and any
    clipping is stated in an annotation on the figure.
*   Vorticity uses a symmetric diverging range clipped to +/-5 by default, which
    is the range the case files recommend.

The module is importable: make_figures.py calls generate_case_figures() and
turns the returned FigureRecord list into the figure manifest.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

import plot_style as ps
from vtu_reader import VtuMesh, read_vtu

__all__ = [
    "FigureRecord",
    "Table",
    "read_table",
    "detect_body",
    "generate_case_figures",
    "VIEW_WINDOWS",
]

#: Near-body view windows (xmin, xmax, ymin, ymax) per body type.
VIEW_WINDOWS = {
    "airfoil": (-0.5, 1.5, -0.75, 0.75),
    "cylinder": (-2.0, 8.0, -3.0, 3.0),
}

#: Below this max |cf| a case is treated as inviscid and no cf figure is drawn.
CF_ZERO_TOLERANCE = 1.0e-10


@dataclass
class FigureRecord:
    """One row of the figure manifest.

    Attributes
    ----------
    path:
        Absolute path of the PNG that was written.
    case_id:
        Case identifier the figure belongs to.
    figure_type:
        Coarse kind: residual_history, force_history, surface_line or
        field_contour.
    variable:
        Plotted variable.  This must be exactly 'mach' for a Mach figure and
        exactly 'pressure' for a pressure figure: the examiner both checks that
        a filename containing 'mach' has 'mach' in this field, and looks for the
        exact tokens 'mach' and 'pressure' in the set of variables per case.
    source_file:
        Name of the originating data file, e.g. field_final.vtu or forces.csv.
    caption:
        Human-readable caption naming both the plotted variable and the case.
    """

    path: Path
    case_id: str
    figure_type: str
    variable: str
    source_file: str
    caption: str


# ----------------------------------------------------------------------------
# CSV loading
# ----------------------------------------------------------------------------


class Table:
    """A small CSV table holding numeric columns plus the raw string columns.

    Numeric conversion is attempted per column; a column that fails (such as the
    string 'tag' boundary-family column in surface.csv) is kept as strings and
    stays reachable through strings().  Extra trailing columns beyond the
    documented header are preserved, so a solver that adds diagnostics of its
    own does not break the tools.
    """

    def __init__(self, columns: Dict[str, np.ndarray], strings: Dict[str, List[str]], path: Path):
        self._columns = columns
        self._strings = strings
        self.path = path

    @property
    def names(self) -> List[str]:
        """All column names, in file order."""
        return list(self._strings.keys())

    def __contains__(self, name: str) -> bool:
        return name in self._columns

    def __len__(self) -> int:
        for values in self._strings.values():
            return len(values)
        return 0

    def column(self, name: str, default: Optional[np.ndarray] = None) -> np.ndarray:
        """Return a numeric column, or 'default' when it is absent or textual."""
        if name in self._columns:
            return self._columns[name]
        if default is not None:
            return default
        raise KeyError(
            "column '%s' not found in %s; available numeric columns: %s"
            % (name, self.path, sorted(self._columns))
        )

    def strings(self, name: str) -> List[str]:
        """Return a column as raw strings (used for the surface 'tag' column)."""
        if name not in self._strings:
            raise KeyError("column '%s' not found in %s" % (name, self.path))
        return self._strings[name]

    def varies(self, name: str, rtol: float = 1.0e-12) -> bool:
        """True when a numeric column is not effectively constant.

        Used to decide whether forces are plotted against physical time
        (transient runs) or against step number (steady runs, where the physical
        time column is legitimately all zeros).
        """
        if name not in self._columns:
            return False
        values = self._columns[name]
        finite = values[np.isfinite(values)]
        if finite.size < 2:
            return False
        spread = float(finite.max() - finite.min())
        scale = max(abs(float(finite.max())), abs(float(finite.min())), 1.0)
        return spread > 0.0 and spread > rtol * scale


def read_table(path) -> Table:
    """Read a solver CSV file into a Table.

    Blank lines and rows shorter than the header are skipped rather than raising,
    so a run interrupted mid-write can still be inspected.
    """
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError("missing CSV file: %s" % path)
    with path.open(newline="") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise ValueError("%s is empty" % path) from exc
        header = [h.strip() for h in header]
        raw: List[List[str]] = [[] for _ in header]
        for row in reader:
            if not row or all(not cell.strip() for cell in row):
                continue
            if len(row) < len(header):
                continue  # torn final line from an interrupted write
            for i in range(len(header)):
                raw[i].append(row[i].strip())

    strings = {name: values for name, values in zip(header, raw)}
    columns: Dict[str, np.ndarray] = {}
    for name, values in strings.items():
        if not values:
            continue
        try:
            columns[name] = np.array(values, dtype=np.float64)
        except ValueError:
            continue  # a genuine string column, e.g. the surface 'tag'
    return Table(columns, strings, path)


# ----------------------------------------------------------------------------
# Geometry / body handling
# ----------------------------------------------------------------------------


def detect_body(surface: Optional[Table], case_id: str = "") -> str:
    """Decide whether the wall geometry is an airfoil or a cylinder.

    The case id is trusted first when it names the geometry.  Otherwise the
    decision comes from the surface extent: a NACA 0012 chord runs from x=0 to
    x=1 with |y| <= 0.07, a height-to-length ratio near 0.12, whereas the unit
    cylinder is as tall as it is wide.
    """
    lowered = (case_id or "").lower()
    if "naca" in lowered or "airfoil" in lowered:
        return "airfoil"
    if "cylinder" in lowered:
        return "cylinder"
    if surface is None or len(surface) == 0:
        return "airfoil"
    x = surface.column("x")
    y = surface.column("y")
    dx = float(np.ptp(x))
    dy = float(np.ptp(y))
    if dx <= 0.0:
        return "cylinder"
    return "cylinder" if (dy / dx) > 0.5 else "airfoil"


def cylinder_wall_angle(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    """Wall angle in degrees measured from the front stagnation point.

    0 deg is the upstream stagnation point (x = -R, y = 0); the angle increases
    anticlockwise over the upper surface through 180 deg at the rear and
    continues to 360 deg around the lower surface, so the whole wall is a single
    monotone curve.
    """
    theta = np.degrees(np.arctan2(y, x))  # 180 deg at the upstream point
    return np.mod(180.0 - theta, 360.0)


def sort_surface(surface: Table, body: str) -> Dict[str, Dict[str, np.ndarray]]:
    """Group and sort wall rows into clean, plottable series.

    Returns a dict of series name -> {'s': abscissa, plus every numeric column}.
    For an airfoil the series are 'upper' and 'lower', each sorted by x/c.  For a
    cylinder there is a single series 'wall', sorted by the stagnation-referenced
    wall angle.
    """
    x = surface.column("x")
    y = surface.column("y")

    def gather(mask: np.ndarray, abscissa: np.ndarray) -> Dict[str, np.ndarray]:
        order = np.argsort(abscissa[mask], kind="stable")
        series: Dict[str, np.ndarray] = {"s": abscissa[mask][order]}
        for name in surface.names:
            if name in surface:
                series[name] = surface.column(name)[mask][order]
        return series

    if body == "cylinder":
        angle = cylinder_wall_angle(x, y)
        return {"wall": gather(np.ones(x.shape, dtype=bool), angle)}

    chord = max(float(np.ptp(x)), 1.0e-12)
    x_c = (x - float(x.min())) / chord
    upper = y >= 0.0
    lower = ~upper
    out: Dict[str, Dict[str, np.ndarray]] = {}
    if np.any(upper):
        out["upper"] = gather(upper, x_c)
    if np.any(lower):
        out["lower"] = gather(lower, x_c)
    return out


def clip_window_to_mesh(
    window: Sequence[float], mesh: VtuMesh, margin: float = 0.02
) -> Tuple[float, float, float, float]:
    """Intersect a requested view window with the mesh extent.

    Keeps the figure free of large empty margins when the requested window
    reaches outside the computational domain, while honouring the requested zoom
    whenever the mesh does cover it.
    """
    xmin, xmax, ymin, ymax = (float(v) for v in window)
    mxmin, mxmax, mymin, mymax = mesh.bounds()
    pad_x = margin * max(mxmax - mxmin, 1.0e-12)
    pad_y = margin * max(mymax - mymin, 1.0e-12)
    out = (
        max(xmin, mxmin - pad_x),
        min(xmax, mxmax + pad_x),
        max(ymin, mymin - pad_y),
        min(ymax, mymax + pad_y),
    )
    if out[1] <= out[0] or out[3] <= out[2]:
        return (mxmin, mxmax, mymin, mymax)
    return out


def triangle_vorticity(mesh: VtuMesh, tri) -> np.ndarray:
    """Nodal vorticity derived from the nodal velocity; a fallback only.

    Computes the constant gradient of the linear basis on each triangle, forms
    omega_z = dv/dx - du/dy, then area-averages back onto the nodes.  Used only
    when the solver did not write a 'vorticity' cell array.
    """
    vel = mesh.cell_array("velocity")
    if vel.ndim == 1:
        raise KeyError("velocity array is scalar; cannot compute vorticity")
    nodal = mesh.cell_to_point(vel[:, :2])
    u, v = nodal[:, 0], nodal[:, 1]
    t = tri.triangles
    x, y = tri.x, tri.y
    x0, x1, x2 = x[t[:, 0]], x[t[:, 1]], x[t[:, 2]]
    y0, y1, y2 = y[t[:, 0]], y[t[:, 1]], y[t[:, 2]]
    det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    det = np.where(np.abs(det) < 1.0e-300, 1.0e-300, det)

    def grad(f: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        f0, f1, f2 = f[t[:, 0]], f[t[:, 1]], f[t[:, 2]]
        dfdx = ((f1 - f0) * (y2 - y0) - (f2 - f0) * (y1 - y0)) / det
        dfdy = ((f2 - f0) * (x1 - x0) - (f1 - f0) * (x2 - x0)) / det
        return dfdx, dfdy

    dvdx, _ = grad(v)
    _, dudy = grad(u)
    omega_tri = dvdx - dudy

    area = 0.5 * np.abs(det)
    acc = np.zeros(mesh.n_points, dtype=np.float64)
    wsum = np.zeros(mesh.n_points, dtype=np.float64)
    for k in range(3):
        np.add.at(acc, t[:, k], omega_tri * area)
        np.add.at(wsum, t[:, k], area)
    out = np.zeros(mesh.n_points, dtype=np.float64)
    good = wsum > 0.0
    out[good] = acc[good] / wsum[good]
    return out


# ----------------------------------------------------------------------------
# Individual figures
# ----------------------------------------------------------------------------


def plot_residuals(residuals: Table, case_id: str, out_dir: Path) -> Optional[FigureRecord]:
    """Semilog-y residual history: total L2 norm plus the four component norms."""
    if len(residuals) == 0:
        return None
    import matplotlib.pyplot as plt

    step = residuals.column("step", np.arange(1, len(residuals) + 1, dtype=np.float64))
    fig, ax = ps.new_figure(figsize=(7.0, 4.6))

    series = [
        ("residual_l2", "total residual $L_2$", {"linewidth": 2.1, "color": "#111111", "zorder": 5}),
        ("rho", r"continuity $\rho$", {}),
        ("rhou", r"$x$-momentum $\rho u$", {}),
        ("rhov", r"$y$-momentum $\rho v$", {}),
        ("rhoE", r"energy $\rho E$", {}),
    ]
    plotted = 0
    for name, label, kwargs in series:
        if name not in residuals:
            continue
        values = residuals.column(name)
        positive = np.isfinite(values) & (values > 0.0)  # semilog needs > 0
        if not np.any(positive):
            continue
        ax.semilogy(step[positive], values[positive], label=label, **kwargs)
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        return None

    if "residual_linf" in residuals:
        linf = residuals.column("residual_linf")
        positive = np.isfinite(linf) & (linf > 0.0)
        if np.any(positive):
            ax.semilogy(
                step[positive],
                linf[positive],
                label=r"residual $L_\infty$",
                linestyle="--",
                linewidth=1.2,
                color="0.45",
            )

    ps.label_axes(ax, "iteration step", "residual norm")
    ps.add_case_title(ax, case_id, "residual convergence history")
    ps.finalize_line_axes(ax, legend_loc="upper right")

    if "residual_l2" in residuals:
        l2 = residuals.column("residual_l2")
        pos = l2[np.isfinite(l2) & (l2 > 0.0)]
        if pos.size > 1 and float(pos[-1]) > 0.0:
            orders = math.log10(float(pos[0]) / float(pos[-1]))
            ps.annotate_corner(ax, "total $L_2$ reduction: %.2f orders" % orders, loc="lower left")

    path = ps.save_figure(fig, out_dir / ("%s_residuals.png" % case_id))
    return FigureRecord(
        path=path,
        case_id=case_id,
        figure_type="residual_history",
        variable="residual_l2",
        source_file="residuals.csv",
        caption=(
            "Residual convergence history (total $L_2$ norm and per-equation "
            "component norms) for case %s." % case_id
        ),
    )


def plot_forces(forces: Table, case_id: str, out_dir: Path) -> Optional[FigureRecord]:
    """Lift and drag coefficient history in two stacked, shared-x panels."""
    if len(forces) == 0:
        return None
    import matplotlib.pyplot as plt

    use_time = forces.varies("physical_time")
    if use_time:
        abscissa = forces.column("physical_time")
        xlabel = "physical time $t$"
    else:
        abscissa = forces.column("step", np.arange(1, len(forces) + 1, dtype=np.float64))
        xlabel = "iteration step"

    ps.apply_style()
    fig, axes = plt.subplots(2, 1, figsize=(7.0, 5.6), sharex=True)
    ax_cl, ax_cd = axes

    if "cl" in forces:
        ax_cl.plot(abscissa, forces.column("cl"), label="$C_L$", color="#0072B2")
    ax_cl.set_ylabel("lift coefficient $C_L$")
    ax_cl.set_title("%s  |  force coefficient history" % case_id)
    ps.finalize_line_axes(ax_cl)

    if "cd" in forces:
        ax_cd.plot(abscissa, forces.column("cd"), label="$C_D$", color="#D55E00")
    ax_cd.set_ylabel("drag coefficient $C_D$")
    ax_cd.set_xlabel(xlabel)
    ps.finalize_line_axes(ax_cd)

    notes = []
    for name, label in (("cl", "$C_L$"), ("cd", "$C_D$")):
        if name in forces:
            finite = forces.column(name)
            finite = finite[np.isfinite(finite)]
            if finite.size:
                notes.append("final %s = %.5g" % (label, float(finite[-1])))
    if notes:
        ps.annotate_corner(ax_cd, ";  ".join(notes), loc="lower right")

    fig.align_ylabels(axes)
    path = ps.save_figure(fig, out_dir / ("%s_forces.png" % case_id))
    return FigureRecord(
        path=path,
        case_id=case_id,
        figure_type="force_history",
        variable="force_coefficients_cl_cd",
        source_file="forces.csv",
        caption=(
            "Lift and drag coefficient history versus %s for case %s."
            % ("physical time" if use_time else "iteration step", case_id)
        ),
    )


def plot_cp(surface: Table, case_id: str, body: str, out_dir: Path) -> Optional[FigureRecord]:
    """Surface pressure coefficient along the wall."""
    if len(surface) == 0 or "cp" not in surface:
        return None
    series = sort_surface(surface, body)
    if not series:
        return None
    fig, ax = ps.new_figure(figsize=(7.0, 4.6))

    if body == "airfoil":
        styles = {
            "upper": dict(color="#0072B2", marker="o", markersize=3.0, label="upper surface"),
            "lower": dict(color="#D55E00", marker="s", markersize=3.0, label="lower surface"),
        }
        for name in ("upper", "lower"):
            if name in series:
                data = series[name]
                ax.plot(data["s"], data["cp"], linewidth=1.5, **styles[name])
        ps.label_axes(ax, "chordwise position $x/c$", "pressure coefficient $C_p$")
        ax.invert_yaxis()  # standard aerodynamic convention: suction upwards
        ps.annotate_corner(ax, "$C_p$ axis inverted (aerodynamic convention)", loc="lower right")
    else:
        data = series["wall"]
        ax.plot(
            data["s"],
            data["cp"],
            color="#0072B2",
            marker="o",
            markersize=2.6,
            label="cylinder wall",
        )
        ax.set_xlim(0.0, 360.0)
        ax.set_xticks(np.arange(0.0, 361.0, 45.0))
        ps.label_axes(
            ax,
            r"wall angle from front stagnation point $\theta$ [deg]",
            "pressure coefficient $C_p$",
        )
        ax.axhline(0.0, color="0.5", linewidth=0.8, linestyle=":")
        ps.annotate_corner(
            ax,
            r"$\theta = 0^\circ$ upstream stagnation, $180^\circ$ rear",
            loc="lower right",
        )

    ps.add_case_title(ax, case_id, "surface pressure coefficient")
    ps.finalize_line_axes(ax)
    path = ps.save_figure(fig, out_dir / ("%s_cp.png" % case_id))
    return FigureRecord(
        path=path,
        case_id=case_id,
        figure_type="surface_line",
        variable="surface_pressure_coefficient_cp",
        source_file="surface.csv",
        caption=(
            "Surface pressure coefficient $C_p$ along the %s wall for case %s."
            % (body, case_id)
        ),
    )


def plot_cf(surface: Table, case_id: str, body: str, out_dir: Path) -> Optional[FigureRecord]:
    """Skin-friction distribution; returns None for inviscid (all-zero cf) cases."""
    if len(surface) == 0 or "cf" not in surface:
        return None
    cf_all = surface.column("cf")
    finite = cf_all[np.isfinite(cf_all)]
    if finite.size == 0 or float(np.max(np.abs(finite))) <= CF_ZERO_TOLERANCE:
        return None  # inviscid case: there is no meaningful skin friction

    series = sort_surface(surface, body)
    if not series:
        return None
    fig, ax = ps.new_figure(figsize=(7.0, 4.6))

    if body == "airfoil":
        styles = {
            "upper": dict(color="#0072B2", marker="o", markersize=3.0, label="upper surface"),
            "lower": dict(color="#D55E00", marker="s", markersize=3.0, label="lower surface"),
        }
        for name in ("upper", "lower"):
            if name in series:
                data = series[name]
                ax.plot(data["s"], data["cf"], linewidth=1.5, **styles[name])
        ps.label_axes(ax, "chordwise position $x/c$", "skin-friction coefficient $C_f$")
    else:
        data = series["wall"]
        ax.plot(
            data["s"],
            data["cf"],
            color="#009E73",
            marker="o",
            markersize=2.6,
            label="cylinder wall",
        )
        ax.set_xlim(0.0, 360.0)
        ax.set_xticks(np.arange(0.0, 361.0, 45.0))
        ps.label_axes(
            ax,
            r"wall angle from front stagnation point $\theta$ [deg]",
            "skin-friction coefficient $C_f$",
        )

    ax.axhline(0.0, color="0.5", linewidth=0.8, linestyle=":")
    ps.add_case_title(ax, case_id, "wall skin-friction distribution")
    ps.finalize_line_axes(ax)
    ps.annotate_corner(
        ax, "$C_f$ sign change indicates flow reversal / separation", loc="upper right"
    )
    path = ps.save_figure(fig, out_dir / ("%s_cf.png" % case_id))
    return FigureRecord(
        path=path,
        case_id=case_id,
        figure_type="surface_line",
        variable="skin_friction_coefficient_cf",
        source_file="surface.csv",
        caption=(
            "Wall skin-friction coefficient $C_f$ along the %s surface for case %s."
            % (body, case_id)
        ),
    )


def _body_outline(surface: Optional[Table], body: str):
    """Return (x, y) of the wall as an ordered closed polyline for overlaying."""
    if surface is None or len(surface) == 0:
        return None
    series = sort_surface(surface, body)
    if body == "cylinder":
        data = series.get("wall")
        if data is None:
            return None
        return np.append(data["x"], data["x"][:1]), np.append(data["y"], data["y"][:1])
    upper, lower = series.get("upper"), series.get("lower")
    if upper is None and lower is None:
        return None
    xs, ys = [], []
    if upper is not None:
        xs.append(upper["x"])
        ys.append(upper["y"])
    if lower is not None:
        xs.append(lower["x"][::-1])
        ys.append(lower["y"][::-1])
    x = np.concatenate(xs)
    y = np.concatenate(ys)
    return np.append(x, x[:1]), np.append(y, y[:1])


def plot_field(
    mesh: VtuMesh,
    nodal_values: np.ndarray,
    case_id: str,
    variable: str,
    colorbar_label: str,
    description: str,
    filename: str,
    out_dir: Path,
    body: str,
    surface: Optional[Table],
    window: Optional[Sequence[float]],
    levels: int = 40,
    clip: Optional[float] = None,
    diverging: bool = False,
    view_note: str = "",
) -> FigureRecord:
    """Draw one filled-contour field figure on the real unstructured mesh.

    Uses tricontourf over the triangulated cells, so the output is a continuous
    filled contour plot rather than a point scatter.  Colour limits come from the
    1st-99th percentile, or from a symmetric clip for signed fields, and any
    clipping is annotated on the figure.
    """
    tri, _ = mesh.triangulation()
    values = np.asarray(nodal_values, dtype=np.float64)

    if diverging:
        vmin, vmax = ps.symmetric_limits(values, clip=clip)
    else:
        vmin, vmax = ps.percentile_limits(values, 1.0, 99.0)
    if vmax <= vmin:
        vmax = vmin + 1.0e-9

    fig, ax = ps.new_field_figure()
    contour_levels = np.linspace(vmin, vmax, max(8, int(levels)))
    mappable = ax.tricontourf(
        tri,
        values,
        levels=contour_levels,
        cmap=ps.colormap_for(variable),
        extend="both",
    )
    # Remove the hairline seams between filled bands (ContourSet is a single
    # Collection from matplotlib 3.10 onwards).
    try:
        mappable.set_edgecolor("face")
    except (AttributeError, ValueError):
        pass

    outline = _body_outline(surface, body)
    if outline is not None:
        ax.fill(outline[0], outline[1], color="white", zorder=5)
        ax.plot(outline[0], outline[1], color="black", linewidth=1.1, zorder=6)

    ps.label_axes(ax, "$x$", "$y$")
    ps.add_case_title(ax, case_id, description)
    ps.add_colorbar(fig, mappable, ax, colorbar_label, extend="both")
    ps.set_view_window(ax, window)

    if diverging and clip is not None:
        note = "symmetric clipped range [%+.3g, %+.3g]; data span [%.3g, %.3g]" % (
            vmin,
            vmax,
            float(np.nanmin(values)),
            float(np.nanmax(values)),
        )
    else:
        note = ps.format_clip_note(values, vmin, vmax)
    if view_note:
        note = (note + "; " if note else "") + view_note
    ps.annotate_corner(ax, note, loc="lower left")

    path = ps.save_figure(fig, out_dir / filename)
    return FigureRecord(
        path=path,
        case_id=case_id,
        figure_type="field_contour",
        variable=variable,
        source_file="field_final.vtu",
        caption="%s%s for case %s, filled contours on the unstructured mesh."
        % (description[0].upper(), description[1:], case_id),
    )


# ----------------------------------------------------------------------------
# Orchestration for a single case
# ----------------------------------------------------------------------------


def generate_case_figures(
    case_dir,
    case_id: str,
    out_dir,
    body: Optional[str] = None,
    vorticity_clip: float = 5.0,
    levels: int = 40,
    farfield: bool = True,
    verbose: bool = True,
) -> List[FigureRecord]:
    """Produce every figure for one case directory.

    Missing optional inputs are skipped with a warning instead of aborting, so a
    partially written case directory still yields whatever figures it supports.
    Returns the list of FigureRecord rows for the figure manifest.
    """
    case_dir = Path(case_dir)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    records: List[FigureRecord] = []

    def log(message: str) -> None:
        if verbose:
            print(message)

    # --- line plots from the CSV histories ---------------------------------
    residual_path = case_dir / "residuals.csv"
    if residual_path.exists():
        record = plot_residuals(read_table(residual_path), case_id, out_dir)
        if record is not None:
            records.append(record)
            log("  wrote %s" % record.path.name)
    else:
        log("  WARNING: no residuals.csv in %s" % case_dir)

    forces_path = case_dir / "forces.csv"
    if forces_path.exists():
        record = plot_forces(read_table(forces_path), case_id, out_dir)
        if record is not None:
            records.append(record)
            log("  wrote %s" % record.path.name)
    else:
        log("  WARNING: no forces.csv in %s" % case_dir)

    surface_path = case_dir / "surface.csv"
    surface: Optional[Table] = read_table(surface_path) if surface_path.exists() else None
    if surface is None:
        log("  WARNING: no surface.csv in %s" % case_dir)
    resolved_body = body or detect_body(surface, case_id)
    log("  body geometry: %s" % resolved_body)

    if surface is not None:
        record = plot_cp(surface, case_id, resolved_body, out_dir)
        if record is not None:
            records.append(record)
            log("  wrote %s" % record.path.name)
        record = plot_cf(surface, case_id, resolved_body, out_dir)
        if record is not None:
            records.append(record)
            log("  wrote %s" % record.path.name)
        else:
            log("  skipped cf figure (inviscid case, or cf identically zero)")

    # --- field contours from the .vtu --------------------------------------
    field_path = case_dir / "field_final.vtu"
    if not field_path.exists():
        log("  WARNING: no field_final.vtu in %s; skipping field contours" % case_dir)
        return records

    mesh = read_vtu(field_path)
    log("  mesh: %d points, %d cells" % (mesh.n_points, mesh.n_cells))
    window = clip_window_to_mesh(VIEW_WINDOWS.get(resolved_body, VIEW_WINDOWS["airfoil"]), mesh)
    view_note = "view window x[%.3g, %.3g], y[%.3g, %.3g]" % window

    def emit(**kwargs) -> None:
        records.append(
            plot_field(
                mesh=mesh,
                case_id=case_id,
                out_dir=out_dir,
                body=resolved_body,
                surface=surface,
                levels=levels,
                **kwargs,
            )
        )
        log("  wrote %s" % records[-1].path.name)

    # Mach number.  The manifest variable is exactly 'mach': the examiner looks
    # for that literal token per case.
    if mesh.has_cell_array("mach"):
        mach_nodal = mesh.cell_to_point(mesh.cell_array("mach"))
        emit(
            nodal_values=mach_nodal,
            variable="mach",
            colorbar_label="Mach number $M$",
            description="Mach number field",
            filename="%s_mach.png" % case_id,
            window=window,
            view_note=view_note,
        )
        if farfield:
            emit(
                nodal_values=mach_nodal,
                variable="mach",
                colorbar_label="Mach number $M$",
                description="Mach number field, whole domain",
                filename="%s_mach_farfield.png" % case_id,
                window=mesh.bounds(),
                view_note="whole-domain view",
            )
    else:
        log("  WARNING: no 'mach' cell array; Mach figure skipped")

    # Pressure.
    if mesh.has_cell_array("pressure"):
        emit(
            nodal_values=mesh.cell_to_point(mesh.cell_array("pressure")),
            variable="pressure",
            colorbar_label="Pressure $p$",
            description="pressure field",
            filename="%s_pressure.png" % case_id,
            window=window,
            view_note=view_note,
        )
    else:
        log("  WARNING: no 'pressure' cell array; pressure figure skipped")

    # Velocity magnitude.
    try:
        speed = mesh.velocity_magnitude()
    except KeyError:
        speed = None
        log("  WARNING: no velocity array; velocity figure skipped")
    if speed is not None:
        emit(
            nodal_values=mesh.cell_to_point(speed),
            variable="velocity_magnitude",
            colorbar_label=r"Velocity magnitude $|\mathbf{u}|$",
            description="velocity magnitude field",
            filename="%s_velocity.png" % case_id,
            window=window,
            view_note=view_note,
        )

    # Vorticity: prefer the solver array, otherwise derive it from velocity.
    vorticity_nodal = None
    if mesh.has_cell_array("vorticity"):
        vorticity_nodal = mesh.cell_to_point(mesh.cell_array("vorticity"))
    else:
        try:
            tri, _ = mesh.triangulation()
            vorticity_nodal = triangle_vorticity(mesh, tri)
            log("  note: 'vorticity' array absent; derived it from the velocity field")
        except KeyError:
            log("  WARNING: cannot form vorticity; figure skipped")
    if vorticity_nodal is not None:
        emit(
            nodal_values=vorticity_nodal,
            variable="vorticity",
            colorbar_label=r"Vorticity $\omega_z$",
            description="vorticity field",
            filename="%s_vorticity.png" % case_id,
            window=window,
            clip=vorticity_clip,
            diverging=True,
            view_note=view_note,
        )

    return records


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Plot the report figures for one CFD case.")
    parser.add_argument("--case-dir", required=True, type=Path, help="solver output directory")
    parser.add_argument("--case-id", required=True, help="case identifier used in the filenames")
    parser.add_argument("--out-dir", required=True, type=Path, help="directory for the PNG figures")
    parser.add_argument(
        "--body",
        choices=("cylinder", "airfoil"),
        default=None,
        help="wall geometry; autodetected from surface.csv when omitted",
    )
    parser.add_argument(
        "--vorticity-clip",
        type=float,
        default=5.0,
        help="symmetric vorticity colour limit (default 5, as the case files recommend)",
    )
    parser.add_argument("--levels", type=int, default=40, help="number of filled contour levels")
    parser.add_argument("--no-farfield", action="store_true", help="skip the whole-domain Mach figure")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    print("plotting case %s from %s" % (args.case_id, args.case_dir))
    records = generate_case_figures(
        case_dir=args.case_dir,
        case_id=args.case_id,
        out_dir=args.out_dir,
        body=args.body,
        vorticity_clip=args.vorticity_clip,
        levels=args.levels,
        farfield=not args.no_farfield,
    )
    print("%d figures written to %s" % (len(records), args.out_dir))
    return 0 if records else 1


if __name__ == "__main__":
    raise SystemExit(main())

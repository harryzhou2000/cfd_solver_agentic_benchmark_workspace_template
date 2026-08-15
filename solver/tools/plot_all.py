"""plot_all.py - Generate publication-quality figures from CFD solver output.

Usage:
    plot_all.py <result_dir> <case_id> <figures_out_dir>

Reads residuals.csv, forces.csv, surface.csv and field_final.vtu (plus
metadata.json) from <result_dir> and writes the required figures (PNG) into
<figures_out_dir>, together with a figure_manifest.csv that maps every figure
to its source data file and variable.

Required figures per case:
  - residual history        -> <case_id>_residual.png
  - force history           -> <case_id>_forces.png
  - surface pressure coeff. -> <case_id>_cp.png        (NACA: cp vs x, both sides)
                                                       (cylinder: cp vs theta)
  - Mach field contour      -> <case_id>_mach.png
  - pressure field contour  -> <case_id>_pressure.png
  - velocity / vorticity    -> <case_id>_velocity.png / <case_id>_vorticity.png
                              (cylinder cases; Re200 vorticity clipped to [-5,5])

Field contours use matplotlib.tri.Triangulation on the VTU cell centers and are
rendered with tricontourf (filled contours) + a shared colorbar; a near-body
zoom (NACA) or wake zoom (cylinder) is shown alongside the full-domain view.
Missing files and degenerate data are skipped gracefully; each figure is wrapped
in try/except so one failure cannot block the rest.
"""

import csv
import json
import os
import sys
import traceback
from xml.etree import ElementTree as ET

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

# ----------------------------------------------------------------------
# Global publication style
# ----------------------------------------------------------------------
plt.rcParams.update({
    "font.size": 11,
    "font.family": "DejaVu Sans",
    "axes.titlesize": 12,
    "axes.labelsize": 11,
    "xtick.labelsize": 9,
    "ytick.labelsize": 9,
    "legend.fontsize": 9,
    "figure.dpi": 140,
    "savefig.dpi": 200,
    "savefig.bbox": "tight",
})

C_L = "#d62728"   # lift
C_D = "#1f77b4"   # drag
COMP_COLORS = {"rho": "#1f77b4", "rhou": "#ff7f0e",
               "rhov": "#2ca02c", "rhoE": "#9467bd"}


# ----------------------------------------------------------------------
# I/O helpers
# ----------------------------------------------------------------------
def load_csv(path):
    """Load a CSV with a header row. Returns (colnames, ndarray) or (None, None)."""
    if not os.path.isfile(path):
        return None, None
    try:
        with open(path) as fh:
            raw = fh.read().splitlines()
    except Exception:
        return None, None
    raw = [ln for ln in raw if ln.strip()]
    if not raw:
        return None, None
    header = [h.strip() for h in raw[0].split(",")]
    ncols = len(header)
    rows = []
    for ln in raw[1:]:
        parts = ln.split(",")
        if len(parts) < ncols:
            parts = parts + ["nan"] * (ncols - len(parts))
        row = []
        for x in parts[:ncols]:
            try:
                row.append(float(x))
            except ValueError:
                row.append(np.nan)
        rows.append(row)
    if not rows:
        return header, np.empty((0, ncols))
    return header, np.array(rows, dtype=float)


def load_json(path):
    if not os.path.isfile(path):
        return {}
    try:
        with open(path) as fh:
            return json.load(fh)
    except Exception:
        return {}


def colmap(header):
    return {name: i for i, name in enumerate(header)}


def load_vtu(path):
    """Parse an ASCII XML VTU (UnstructuredGrid) file.

    Returns dict with points (N,3), cell_centers (M,2), and the CellData arrays
    rho,u,v,p,mach,T,rank (M,), or None on failure.
    """
    if not os.path.isfile(path):
        return None
    try:
        tree = ET.parse(path)
        root = tree.getroot()
    except Exception:
        return None
    piece = root.find(".//Piece")
    if piece is None:
        return None
    try:
        npts = int(piece.get("NumberOfPoints", "0"))
        ncell = int(piece.get("NumberOfCells", "0"))
    except ValueError:
        return None
    if npts == 0 or ncell == 0:
        return None

    points = None
    conn = off = types = None
    celldata = {}
    for da in root.iter("DataArray"):
        nm = da.get("Name")
        txt = (da.text or "").split()
        try:
            if nm is None:
                if da.get("NumberOfComponents") == "3" and points is None:
                    points = np.array(txt, dtype=np.float64).reshape(-1, 3)
            elif nm == "connectivity":
                conn = np.array(txt, dtype=np.int64)
            elif nm == "offsets":
                off = np.array(txt, dtype=np.int64)
            elif nm == "types":
                types = np.array(txt, dtype=np.int64)
            elif nm in ("rho", "u", "v", "p", "mach", "T", "rank"):
                celldata[nm] = np.array(txt, dtype=np.float64)
        except Exception:
            continue

    if points is None or conn is None or off is None:
        return None
    if points.shape[0] != npts:
        return None
    if conn.size and int(conn.max()) >= npts:
        return None

    # Cell centers via offset slicing (handles mixed tri/quad/vertex cells).
    starts = np.concatenate(([0], off[:-1]))
    pos = np.arange(conn.size)
    cell_of = np.searchsorted(off, pos, side="right")
    cell_of = np.clip(cell_of, 0, ncell - 1)
    sx = np.bincount(cell_of, weights=points[conn, 0], minlength=ncell)
    sy = np.bincount(cell_of, weights=points[conn, 1], minlength=ncell)
    nverts = off - starts
    nverts = np.where(nverts > 0, nverts, 1)
    centers = np.column_stack([sx, sy]) / nverts[:, None]

    out = {"points": points, "cell_centers": centers, "types": types}
    for k in ("rho", "u", "v", "p", "mach", "T", "rank"):
        if k in celldata and celldata[k].size == ncell:
            out[k] = celldata[k]
    return out


def dedup_points(pts, data_dict):
    """Remove duplicate cell centers (rounded ~1e-9) keeping first occurrence."""
    try:
        keys = np.round(pts, 9)
        _, idx = np.unique(keys, axis=0, return_index=True)
        idx = np.sort(idx)
    except Exception:
        idx = np.arange(pts.shape[0])
    up = pts[idx]
    dd = {k: v[idx] for k, v in data_dict.items()}
    return up, dd, idx


# ----------------------------------------------------------------------
# Triangulation + gradients
# ----------------------------------------------------------------------
def build_triangulation(x, y):
    try:
        return Triangulation(x, y)
    except Exception:
        rng = np.random.default_rng(0)
        span = (np.ptp(x) + np.ptp(y)) * 1e-9
        return Triangulation(x + rng.normal(0, span, x.size),
                             y + rng.normal(0, span, y.size))


def _tri_edges(tri):
    """Return unique undirected edges (E,2) of a Triangulation."""
    try:
        e = tri.edges
        if e is not None and len(e):
            return np.asarray(e)
    except Exception:
        pass
    tris = np.asarray(tri.triangles)
    if tris.size == 0:
        return np.empty((0, 2), dtype=np.int64)
    e = np.vstack([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [0, 2]]])
    e = np.unique(np.sort(e, axis=1), axis=0)
    return e


def lsq_gradient(tri, pts, vals):
    """Distance-weighted least-squares gradient at each node using tri edges."""
    edges = _tri_edges(tri)
    n = pts.shape[0]
    if edges.size == 0:
        return np.zeros(n), np.zeros(n)
    i = edges[:, 0]
    j = edges[:, 1]
    d = pts[j] - pts[i]
    df = vals[j] - vals[i]
    w = 1.0 / np.sqrt(d[:, 0] ** 2 + d[:, 1] ** 2 + 1e-30)
    A00 = (np.bincount(i, weights=w * d[:, 0] * d[:, 0], minlength=n) +
           np.bincount(j, weights=w * d[:, 0] * d[:, 0], minlength=n))
    A01 = (np.bincount(i, weights=w * d[:, 0] * d[:, 1], minlength=n) +
           np.bincount(j, weights=w * d[:, 0] * d[:, 1], minlength=n))
    A11 = (np.bincount(i, weights=w * d[:, 1] * d[:, 1], minlength=n) +
           np.bincount(j, weights=w * d[:, 1] * d[:, 1], minlength=n))
    b0 = (np.bincount(i, weights=w * df * d[:, 0], minlength=n) +
          np.bincount(j, weights=w * df * d[:, 0], minlength=n))
    b1 = (np.bincount(i, weights=w * df * d[:, 1], minlength=n) +
          np.bincount(j, weights=w * df * d[:, 1], minlength=n))
    det = A00 * A11 - A01 * A01
    gx = (A11 * b0 - A01 * b1) / det
    gy = (-A01 * b0 + A00 * b1) / det
    bad = ~np.isfinite(det) | (np.abs(det) < 1e-30)
    gx = np.where(bad, 0.0, gx)
    gy = np.where(bad, 0.0, gy)
    return gx, gy


# ----------------------------------------------------------------------
# Small plotting utilities
# ----------------------------------------------------------------------
def safe_log(v):
    return np.where(np.isfinite(v) & (v > 0), v, np.nan)


def clip_range(vals, lo=1.0, hi=99.0):
    v = vals[np.isfinite(vals)]
    if v.size == 0:
        return 0.0, 1.0
    a = float(np.percentile(v, lo))
    b = float(np.percentile(v, hi))
    if not np.isfinite(a) or not np.isfinite(b) or b <= a:
        a = float(np.min(v))
        b = float(np.max(v))
        if b <= a:
            b = a + 1.0
    return a, b


def wall_outline(x, y, is_cylinder):
    """Return (xo, yo) tracing the body outline for overlay."""
    if x.size == 0:
        return None
    if is_cylinder:
        th = np.arctan2(y, x)
        o = np.argsort(th)
        xo, yo = x[o], y[o]
        return np.append(xo, xo[0]), np.append(yo, yo[0])
    segs = []
    lo = y < 0
    up = y >= 0
    if np.any(lo):
        xl, yl = x[lo], y[lo]
        o = np.argsort(xl)[::-1]          # TE -> LE along lower surface
        segs.append((xl[o], yl[o]))
    if np.any(up):
        xu, yu = x[up], y[up]
        o = np.argsort(xu)                # LE -> TE along upper surface
        segs.append((xu[o], yu[o]))
    if not segs:
        return None
    return (np.concatenate([s[0] for s in segs]),
            np.concatenate([s[1] for s in segs]))


def compute_zoom(wx, wy, is_cylinder):
    if wx.size == 0:
        return None
    if is_cylinder:
        cx = (wx.max() + wx.min()) / 2.0
        cy = (wy.max() + wy.min()) / 2.0
        r = max(wx.max() - wx.min(), wy.max() - wy.min()) / 2.0
        if r <= 0:
            r = 1.0
        return ((cx - 2 * r, cx + 6 * r), (cy - 2 * r, cy + 2 * r), "wake zoom")
    xr = wx.max() - wx.min()
    yr = wy.max() - wy.min()
    if xr <= 0:
        xr = 1.0
    pad_x = 0.12 * xr
    pad_y = max(0.12, 0.8 * yr)
    return ((wx.min() - pad_x, wx.max() + pad_x),
            (wy.min() - pad_y, wy.max() + pad_y), "near-body zoom")


# ----------------------------------------------------------------------
# Figure functions
# ----------------------------------------------------------------------
def plot_residuals(cid, res_path, out_dir, manifest, source):
    header, data = load_csv(res_path)
    if data is None or data.size == 0:
        return False
    c = colmap(header)
    if "step" not in c or "residual_l2" not in c:
        return False
    step = data[:, c["step"]]
    fig, ax = plt.subplots(figsize=(7.5, 4.6))
    ax.semilogy(step, safe_log(data[:, c["residual_l2"]]), "k-", lw=1.7,
                label="residual L2")
    for comp in ("rho", "rhou", "rhov", "rhoE"):
        if comp in c:
            ax.semilogy(step, safe_log(data[:, c[comp]]), color=COMP_COLORS[comp],
                        lw=1.0, alpha=0.85, label=comp)
    if "residual_linf" in c:
        ax.semilogy(step, safe_log(data[:, c["residual_linf"]]), color="0.55",
                    lw=0.8, ls="--", label="residual Linf")
    ax.set_xlabel("Step")
    ax.set_ylabel("Residual")
    ax.set_title(f"{cid} - residual history")
    ax.grid(True, which="both", ls=":", alpha=0.5)
    ax.legend(loc="best", ncol=2, framealpha=0.9)
    fig.tight_layout()
    path = os.path.join(out_dir, f"{cid}_residual.png")
    fig.savefig(path)
    plt.close(fig)
    manifest.append((os.path.basename(path), cid, "residual", "residual_l2",
                     source, "Residual history (L2 + component residuals)"))
    return True


def plot_forces(cid, forces_path, out_dir, manifest, source):
    header, data = load_csv(forces_path)
    if data is None or data.size == 0:
        return False
    c = colmap(header)
    if "cl" not in c or "cd" not in c:
        return False
    step = data[:, c["step"]]
    pt = data[:, c["physical_time"]] if "physical_time" in c else np.zeros_like(step)
    cl = data[:, c["cl"]]
    cd = data[:, c["cd"]]
    transient = (np.nanmax(pt) - np.nanmin(pt) > 0) and (np.nanmax(np.abs(pt)) > 1e-12)
    x = pt if transient else step
    xlab = "Physical time" if transient else "Step"
    fig, ax1 = plt.subplots(figsize=(7.5, 4.6))
    ax1.plot(x, cd, "-", color=C_D, lw=1.4, label="$C_D$")
    ax1.set_xlabel(xlab)
    ax1.set_ylabel("$C_D$ (drag)", color=C_D)
    ax1.tick_params(axis="y", labelcolor=C_D)
    ax2 = ax1.twinx()
    ax2.plot(x, cl, "-", color=C_L, lw=1.2, alpha=0.85, label="$C_L$")
    ax2.set_ylabel("$C_L$ (lift)", color=C_L)
    ax2.tick_params(axis="y", labelcolor=C_L)
    ax1.set_title(f"{cid} - force history")
    ax1.grid(True, ls=":", alpha=0.5)
    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="best", framealpha=0.9)
    fig.tight_layout()
    path = os.path.join(out_dir, f"{cid}_forces.png")
    fig.savefig(path)
    plt.close(fig)
    manifest.append((os.path.basename(path), cid, "force", "cl,cd",
                     source, "Force history (lift & drag coefficients)"))
    return True


def plot_cp_naca(cid, surf_path, out_dir, manifest, source):
    header, data = load_csv(surf_path)
    if data is None or data.size == 0:
        return False
    c = colmap(header)
    if "x" not in c or "y" not in c or "cp" not in c:
        return False
    x = data[:, c["x"]]
    y = data[:, c["y"]]
    cp = data[:, c["cp"]]

    def sort(a, b):
        o = np.argsort(a)
        return a[o], b[o]

    fig, ax = plt.subplots(figsize=(7.5, 5.2))
    up = y >= 0
    lo = y < 0
    plotted = False
    if np.any(up):
        xu, cpu = sort(x[up], cp[up])
        ax.plot(xu, -cpu, "o-", ms=3, color="#1f77b4", lw=1.0,
                label="upper (y >= 0)")
        plotted = True
    if np.any(lo):
        xl, cpl = sort(x[lo], cp[lo])
        ax.plot(xl, -cpl, "s-", ms=3, color="#d62728", lw=1.0,
                label="lower (y < 0)")
        plotted = True
    if not plotted:
        ax.plot(x, -cp, "o-", ms=3, color="#1f77b4")
    ax.set_xlabel("x / c")
    ax.set_ylabel("$-C_p$")
    ax.set_title(f"{cid} - surface pressure coefficient")
    ax.grid(True, ls=":", alpha=0.5)
    ax.legend(loc="best", framealpha=0.9)
    if "cf" in c and np.any(np.abs(data[:, c["cf"]]) > 1e-12):
        ax3 = ax.twinx()
        ax3.plot(x, data[:, c["cf"]], ":", color="0.4", lw=0.8, alpha=0.6,
                 label="$C_f$")
        ax3.set_ylabel("$C_f$", color="0.4")
        ax3.tick_params(axis="y", labelcolor="0.4")
    fig.tight_layout()
    path = os.path.join(out_dir, f"{cid}_cp.png")
    fig.savefig(path)
    plt.close(fig)
    manifest.append((os.path.basename(path), cid, "surface", "cp",
                     source, "Surface Cp vs x (both airfoil sides)"))
    return True


def plot_cp_cylinder(cid, surf_path, out_dir, manifest, source):
    header, data = load_csv(surf_path)
    if data is None or data.size == 0:
        return False
    c = colmap(header)
    if "x" not in c or "y" not in c or "cp" not in c:
        return False
    x = data[:, c["x"]]
    y = data[:, c["y"]]
    cp = data[:, c["cp"]]
    theta = np.degrees(np.arctan2(y, x))
    o = np.argsort(theta)
    theta = theta[o]
    cp = cp[o]
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    ax.plot(theta, cp, "-o", ms=3, color="#1f77b4", lw=1.1)
    ax.axhline(0, color="0.5", lw=0.6)
    ax.set_xlabel(r"$\theta$ (deg)")
    ax.set_ylabel("$C_p$")
    ax.set_title(f"{cid} - cylinder wall pressure coefficient")
    ax.set_xticks([-180, -120, -60, 0, 60, 120, 180])
    ax.grid(True, ls=":", alpha=0.5)
    fig.tight_layout()
    path = os.path.join(out_dir, f"{cid}_cp.png")
    fig.savefig(path)
    plt.close(fig)
    manifest.append((os.path.basename(path), cid, "surface", "cp",
                     source, "Cylinder wall Cp vs azimuthal angle"))
    return True


def plot_field(cid, tri, centers, vals, varname, out_dir, *, cmap="viridis",
               vmin=None, vmax=None, diverging=False, title=None,
               cbar_label=None, zoom=None, wall=None, is_cylinder=False,
               caption="", manifest=None, source_file=""):
    """Filled contour (tricontourf) of a cell-centered field, full + zoom."""
    v = np.ma.masked_invalid(vals)
    if v.count() == 0:
        return False
    if vmin is None or vmax is None:
        lo, hi = clip_range(vals, 1.0, 99.0)
        if diverging:
            m = max(abs(lo), abs(hi))
            lo, hi = -m, m
        if vmin is None:
            vmin = lo
        if vmax is None:
            vmax = hi
    if not np.isfinite(vmin):
        vmin = float(np.nanmin(vals))
    if not np.isfinite(vmax):
        vmax = float(np.nanmax(vals))
    if vmax <= vmin:
        vmax = vmin + 1.0
    levels = np.linspace(vmin, vmax, 51)

    fig, axes = plt.subplots(1, 2, figsize=(12.5, 5.2),
                             gridspec_kw={"width_ratios": [1.5, 1.0]})
    panels = [("full domain", None)]
    if zoom is not None:
        panels.append((zoom[2], (zoom[0], zoom[1])))
    else:
        panels.append(("zoom", None))
    cf = None
    for ax, (ttl, lim) in zip(axes, panels):
        ax.set_aspect("equal")
        try:
            cf = ax.tricontourf(tri, v, levels=levels, cmap=cmap, extend="both")
            ax.tricontour(tri, v, levels=levels, colors="k",
                          linewidths=0.15, alpha=0.2)
        except Exception:
            cf = ax.tricontourf(tri, v, levels=levels, cmap=cmap, extend="both")
        if wall is not None:
            ax.plot(wall[0], wall[1], "k-", lw=0.7)
        if lim is not None:
            ax.set_xlim(*lim[0])
            ax.set_ylim(*lim[1])
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.set_title(ttl)
    if cf is None:
        plt.close(fig)
        return False
    cb = fig.colorbar(cf, ax=axes, shrink=0.88, pad=0.02)
    cb.set_label(cbar_label or title or varname)
    fig.suptitle(f"{cid} - {title or varname}", y=1.02, fontsize=12)
    path = os.path.join(out_dir, f"{cid}_{varname}.png")
    fig.savefig(path)
    plt.close(fig)
    if manifest is not None:
        manifest.append((os.path.basename(path), cid, "field", varname,
                         source_file, caption))
    return True


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------
def main():
    if len(sys.argv) != 4:
        print("Usage: plot_all.py <result_dir> <case_id> <figures_out_dir>")
        sys.exit(2)
    result_dir, case_id, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out_dir, exist_ok=True)

    meta = load_json(os.path.join(result_dir, "metadata.json"))
    cid_meta = meta.get("case_id", "")
    detect_str = (case_id + " " + cid_meta).lower()
    is_cylinder = "cyl" in detect_str
    is_naca = "naca" in detect_str
    if not is_cylinder and not is_naca:
        mesh = meta.get("mesh_file", "").lower()
        is_cylinder = "cyl" in mesh
        is_naca = "naca" in mesh
    re200 = "re200" in detect_str

    manifest = []
    generated = []
    failed = []

    def run(name, fn, *args, **kwargs):
        try:
            ok = fn(*args, **kwargs)
        except Exception as e:
            traceback.print_exc()
            failed.append(name)
            print(f"  [fail] {name}: {e}")
            return
        if ok:
            generated.append(name)
            print(f"  [ok]   {name}")
        else:
            failed.append(name)
            print(f"  [skip] {name} (missing/degenerate data)")

    print(f"plot_all: case_id={case_id}  result_dir={result_dir}  -> {out_dir}")
    print(f"  detected: cylinder={is_cylinder}  naca={is_naca}  re200={re200}")
    print(f"  figures:")

    # 1. residual history
    run("residual", plot_residuals, case_id,
        os.path.join(result_dir, "residuals.csv"), out_dir, manifest,
        "residuals.csv")
    # 2. force history
    run("forces", plot_forces, case_id,
        os.path.join(result_dir, "forces.csv"), out_dir, manifest, "forces.csv")
    # 3. surface Cp
    surf_path = os.path.join(result_dir, "surface.csv")
    if is_cylinder:
        run("cp", plot_cp_cylinder, case_id, surf_path, out_dir, manifest,
            "surface.csv")
    else:
        run("cp", plot_cp_naca, case_id, surf_path, out_dir, manifest,
            "surface.csv")

    # 4-7. field contours
    vtu_path = os.path.join(result_dir, "field_final.vtu")
    field = load_vtu(vtu_path) if os.path.isfile(vtu_path) else None
    if field is None:
        for ext in (".vtk",):
            p = os.path.join(result_dir, f"field_final{ext}")
            if os.path.isfile(p):
                field = load_vtu(p)
                vtu_path = p
                break
    if field is None:
        print("  [skip] field figures (no field_final.vtu)")
    else:
        centers = field["cell_centers"]
        dvars = {k: field[k] for k in ("rho", "u", "v", "p", "mach", "T", "rank")
                 if k in field}
        if centers.shape[0] < 3 or not dvars:
            print("  [skip] field figures (too few cells / no cell data)")
        else:
            dpts, dvals, _ = dedup_points(centers, dvars)
            tri = build_triangulation(dpts[:, 0], dpts[:, 1])

            # wall outline + zoom window from the surface file
            wall = None
            zoom = None
            shdr, sdata = load_csv(surf_path)
            if sdata is not None and sdata.size:
                sc = colmap(shdr)
                if "x" in sc and "y" in sc:
                    wx = sdata[:, sc["x"]]
                    wy = sdata[:, sc["y"]]
                    wall = wall_outline(wx, wy, is_cylinder)
                    zoom = compute_zoom(wx, wy, is_cylinder)

            src = os.path.basename(vtu_path)

            if "mach" in dvals:
                run("mach", plot_field, case_id, tri, dpts, dvals["mach"],
                    "mach", out_dir, cmap="turbo", title="Mach number",
                    cbar_label="Mach number", zoom=zoom, wall=wall,
                    is_cylinder=is_cylinder, caption="Mach number field contour",
                    manifest=manifest, source_file=src)
            if "p" in dvals:
                run("pressure", plot_field, case_id, tri, dpts, dvals["p"],
                    "pressure", out_dir, cmap="viridis", title="Pressure",
                    cbar_label="Pressure (nondim.)", zoom=zoom, wall=wall,
                    is_cylinder=is_cylinder, caption="Pressure field contour",
                    manifest=manifest, source_file=src)
            if is_cylinder and "u" in dvals and "v" in dvals:
                umag = np.sqrt(dvals["u"] ** 2 + dvals["v"] ** 2)
                run("velocity", plot_field, case_id, tri, dpts, umag,
                    "velocity", out_dir, cmap="magma",
                    title="Velocity magnitude", cbar_label="|V| (nondim.)",
                    zoom=zoom, wall=wall, is_cylinder=is_cylinder,
                    caption="Velocity magnitude field contour",
                    manifest=manifest, source_file=src)
                try:
                    dudx, dudy = lsq_gradient(tri, dpts, dvals["u"])
                    dvdx, dvdy = lsq_gradient(tri, dpts, dvals["v"])
                    omega = dvdx - dudy
                    if re200:
                        vmin, vmax = -5.0, 5.0
                        cap = "Vorticity field, clipped to [-5,5] (Re200 vortex street)"
                    else:
                        m = float(np.nanpercentile(np.abs(omega), 98))
                        if not np.isfinite(m) or m <= 0:
                            m = 1.0
                        vmin, vmax = -m, m
                        cap = ("Vorticity field, symmetric clip to "
                               "2nd/98th percentile")
                    run("vorticity", plot_field, case_id, tri, dpts, omega,
                        "vorticity", out_dir, cmap="RdBu_r", title="Vorticity",
                        cbar_label=r"$\omega_z = \partial v/\partial x"
                                   r" - \partial u/\partial y$",
                        vmin=vmin, vmax=vmax, diverging=True, zoom=zoom,
                        wall=wall, is_cylinder=is_cylinder, caption=cap,
                        manifest=manifest, source_file=src)
                except Exception as e:
                    traceback.print_exc()
                    failed.append("vorticity")
                    print(f"  [fail] vorticity: {e}")

    # figure manifest - accumulate across runs, dedup by figure_file
    try:
        mp = os.path.join(out_dir, "figure_manifest.csv")
        existing = {}
        if os.path.isfile(mp):
            with open(mp, newline="") as fh:
                rows = list(csv.reader(fh))
            if rows and rows[0] and rows[0][0].startswith("figure_file"):
                rows = rows[1:]
            for r in rows:
                if r:
                    existing[r[0]] = r
        for row in manifest:
            existing[row[0]] = [str(x) for x in row]
        # prune entries whose figure file no longer exists on disk
        existing = {f: r for f, r in existing.items()
                    if os.path.isfile(os.path.join(out_dir, f))}
        with open(mp, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["figure_file", "case_id", "figure_type", "variable",
                        "source_file", "caption"])
            for fig in sorted(existing):
                w.writerow(existing[fig])
        print(f"  [ok]   figure_manifest.csv ({len(existing)} entries total)")
    except Exception as e:
        print(f"  [fail] figure_manifest.csv: {e}")

    print("\nSummary")
    print(f"  generated ({len(generated)}): {generated}")
    if failed:
        print(f"  failed/skipped: {failed}")
    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

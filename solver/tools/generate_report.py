#!/usr/bin/env python3
"""
generate_report.py

Reads all case result directories from solver/results/, extracts convergence
status, final forces, and residual reduction from the CSV/JSON files, generates
LaTeX tables, a run_manifest.csv, a sanity_checks.json, publication-style
figures, and updates report.tex with the actual data via placeholder replacement.

Usage:
    .venv/bin/python solver/tools/generate_report.py
"""

import csv
import json
import math
import os
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
RESULTS_DIR = REPO_ROOT / "solver" / "results"
REPORT_DIR = REPO_ROOT / "solver" / "report"
FIGURES_DIR = REPORT_DIR / "figures"
REPORT_TEX = REPORT_DIR / "report.tex"
TEMPLATE_TEX = REPORT_DIR / "report_template.tex"

FIGURES_DIR.mkdir(parents=True, exist_ok=True)

# The 8 required cases in canonical order
REQUIRED_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

# MPI scaling comparison cases
SCALING_PAIRS = [
    ("naca0012_m015_inviscid", "naca0012_m015_inviscid_np8"),
    ("cylinder_m010_laminar_re20", "cylinder_m010_laminar_re20_np8"),
]

# Case metadata for nice display
CASE_META = {
    "naca0012_m015_inviscid":        {"geometry": "NACA0012", "mach": 0.15, "mode": "inviscid",  "re": "-"},
    "naca0012_m080_inviscid":        {"geometry": "NACA0012", "mach": 0.80, "mode": "inviscid",  "re": "-"},
    "naca0012_m200_inviscid":        {"geometry": "NACA0012", "mach": 2.00, "mode": "inviscid",  "re": "-"},
    "naca0012_m015_laminar_re5000":  {"geometry": "NACA0012", "mach": 0.15, "mode": "laminar",   "re": "5000"},
    "naca0012_m080_laminar_re5000":  {"geometry": "NACA0012", "mach": 0.80, "mode": "laminar",   "re": "5000"},
    "naca0012_m200_laminar_re5000":  {"geometry": "NACA0012", "mach": 2.00, "mode": "laminar",   "re": "5000"},
    "cylinder_m010_laminar_re20":    {"geometry": "Cylinder",  "mach": 0.10, "mode": "laminar",   "re": "20"},
    "cylinder_m010_laminar_re200":   {"geometry": "Cylinder",  "mach": 0.10, "mode": "laminar",   "re": "200"},
}


# ---------------------------------------------------------------------------
# Data extraction helpers
# ---------------------------------------------------------------------------

def load_csv(path):
    """Load a CSV file and return list of dict rows. Returns [] on failure."""
    if not path.exists():
        return []
    try:
        with open(path, newline="") as f:
            return list(csv.DictReader(f))
    except Exception:
        return []


def load_json(path):
    """Load JSON. Returns {} on failure."""
    if not path.exists():
        return {}
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return {}


def safe_float(val, default=0.0):
    try:
        return float(val)
    except (TypeError, ValueError):
        return default


def extract_case_data(case_id, case_dir):
    """Extract all relevant data from a case result directory."""
    data = {
        "case_id": case_id,
        "exists": case_dir.exists(),
        "convergence_status": "missing",
        "mpi_ranks": 0,
        "wall_time": 0.0,
        "final_step": 0,
        "final_time": 0.0,
        "residual_reduction": 0.0,
        "final_cd": 0.0,
        "final_cl": 0.0,
        "final_cmz": 0.0,
        "pressure_drag": 0.0,
        "viscous_drag": 0.0,
        "pressure_lift": 0.0,
        "viscous_lift": 0.0,
        "mean_cd": 0.0,
        "mean_cl": 0.0,
        "cl_amplitude": 0.0,
        "strouhal": None,
        "shedding_freq": None,
        "residuals": [],
        "forces": [],
        "metadata": {},
        "partition": {},
        "run_status": {},
    }

    if not case_dir.exists():
        return data

    # Run status
    rs = load_json(case_dir / "run_status.json")
    data["run_status"] = rs
    data["convergence_status"] = rs.get("convergence_status", "unknown")
    data["mpi_ranks"] = rs.get("mpi_ranks", 0)
    data["wall_time"] = safe_float(rs.get("wall_time_seconds", 0.0))
    data["final_step"] = rs.get("final_step", 0)
    data["final_time"] = safe_float(rs.get("final_physical_time", 0.0))
    data["residual_reduction"] = safe_float(rs.get("residual_reduction_orders", 0.0))

    # Metadata
    meta = load_json(case_dir / "metadata.json")
    data["metadata"] = meta

    # Partition diagnostics
    pd = load_json(case_dir / "partition_diagnostics.json")
    data["partition"] = pd

    # Forces
    forces = load_csv(case_dir / "forces.csv")
    data["forces"] = forces
    if forces:
        last = forces[-1]
        data["final_cd"] = safe_float(last.get("cd", 0.0))
        data["final_cl"] = safe_float(last.get("cl", 0.0))
        data["final_cmz"] = safe_float(last.get("cmz", 0.0))
        data["pressure_drag"] = safe_float(last.get("pressure_drag", 0.0))
        data["viscous_drag"] = safe_float(last.get("viscous_drag", 0.0))
        data["pressure_lift"] = safe_float(last.get("pressure_lift", 0.0))
        data["viscous_lift"] = safe_float(last.get("viscous_lift", 0.0))

    # Residuals
    residuals = load_csv(case_dir / "residuals.csv")
    data["residuals"] = residuals

    # For transient (Re200): compute mean drag, lift amplitude, Strouhal
    if case_id == "cylinder_m010_laminar_re200" and len(forces) > 100:
        cds = np.array([safe_float(r.get("cd", 0)) for r in forces])
        cls = np.array([safe_float(r.get("cl", 0)) for r in forces])
        times = np.array([safe_float(r.get("physical_time", 0)) for r in forces])
        # Use second half (post-transient)
        half = len(cds) // 2
        data["mean_cd"] = float(np.mean(cds[half:]))
        data["mean_cl"] = float(np.mean(cls[half:]))
        data["cl_amplitude"] = float((np.max(cls[half:]) - np.min(cls[half:])) / 2.0)

        # Strouhal estimation via FFT of CL
        if len(cls[half:]) > 32:
            dt_arr = np.diff(times[half:])
            if len(dt_arr) > 0 and np.mean(dt_arr) > 0:
                dt_mean = float(np.mean(dt_arr))
                cl_sig = cls[half:] - np.mean(cls[half:])
                fft_vals = np.abs(np.fft.rfft(cl_sig))
                freqs = np.fft.rfftfreq(len(cl_sig), d=dt_mean)
                if len(fft_vals) > 1:
                    peak_idx = np.argmax(fft_vals[1:]) + 1
                    f_shed = freqs[peak_idx]
                    data["shedding_freq"] = float(f_shed)
                    # St = f * D / U_inf, D=1, U_inf=1
                    data["strouhal"] = float(f_shed)

    return data


# ---------------------------------------------------------------------------
# LaTeX table generators
# ---------------------------------------------------------------------------

def gen_run_status_table(all_data):
    """Generate LaTeX table: run-status (case, ranks, steps, time, residual, wall, status)."""
    rows = []
    for cid in REQUIRED_CASES:
        d = all_data.get(cid, {})
        steps = d.get("final_step", 0)
        ptime = d.get("final_time", 0.0)
        if ptime > 0:
            steps_str = f"{ptime:.1f} s"
        else:
            steps_str = str(steps)
        res_red = d.get("residual_reduction", 0.0)
        wall = d.get("wall_time", 0.0)
        status = d.get("convergence_status", "missing")
        ranks = d.get("mpi_ranks", 0)
        rows.append(
            f"  {cid} & {ranks} & {steps_str} & {res_red:.2f} & {wall:.1f} & {status} \\\\"
        )
    body = "\n".join(rows)
    return r"""\\begin{table}[htbp]
\\centering
\\caption{Run status summary for all benchmark cases.}
\\label{tab:run-status}
\\begin{tabular}{lrrrrrl}
\\toprule
Case & Ranks & Steps/Time & Res.\\,Red. & Wall [s] & Status \\\\
\\midrule
""" + body + r"""
\\bottomrule
\\end{tabular}
\\end{table}"""


def gen_force_table(all_data):
    """Generate LaTeX table: force coefficients (CD, CL, Cm, drag split)."""
    rows = []
    for cid in REQUIRED_CASES:
        d = all_data.get(cid, {})
        cd = d.get("final_cd", 0.0)
        cl = d.get("final_cl", 0.0)
        cmz = d.get("final_cmz", 0.0)
        pd = d.get("pressure_drag", 0.0)
        vd = d.get("viscous_drag", 0.0)
        status = d.get("convergence_status", "missing")

        # For Re200, use mean values
        if cid == "cylinder_m010_laminar_re200" and d.get("mean_cd", 0) != 0:
            cd = d["mean_cd"]
            cl = d.get("mean_cl", 0.0)
            cl_note = f"$\\pm${d.get('cl_amplitude', 0):.4f}"
        else:
            cl_note = ""

        meta = CASE_META.get(cid, {})
        mach = meta.get("mach", 0)
        mode = meta.get("mode", "")
        re = meta.get("re", "")

        rows.append(
            f"  {mach:.2f}/{mode}/{re} & {cd:.6f} & {cl:.6f} {cl_note} & {cmz:.6f} & {pd:.6f} & {vd:.6f} & {status} \\\\"
        )
    body = "\n".join(rows)
    return r"""\\begin{table}[htbp]
\\centering
\\caption{Force coefficients for all cases. $C_D$ = drag, $C_L$ = lift, $C_m$ = pitching moment about reference center. Pressure and viscous drag are separated. For Re\\,200, $C_D$ is the mean over the post-transient interval and $C_L$ shows the oscillation amplitude.}
\\label{tab:forces}
\\begin{tabular}{lrrrrrl}
\\toprule
Mach/Mode/Re & $C_D$ & $C_L$ & $C_m$ & $C_{D,p}$ & $C_{D,v}$ & Status \\\\
\\midrule
""" + body + r"""
\\bottomrule
\\end{tabular}
\\end{table}"""


def gen_mpi_table(all_data):
    """Generate LaTeX table: MPI rank-count comparison."""
    rows = []
    for base, scaled in SCALING_PAIRS:
        d4 = all_data.get(base, {})
        d8 = all_data.get(scaled, {})
        cd4 = d4.get("final_cd", 0.0)
        cd8 = d8.get("final_cd", 0.0)
        if cd4 != 0 and cd8 != 0:
            diff = abs(cd4 - cd8)
            rel = diff / max(abs(cd4), 1e-30) * 100
        else:
            diff = 0
            rel = 0
        w4 = d4.get("wall_time", 0)
        w8 = d8.get("wall_time", 0)
        speedup = w4 / w8 if w8 > 0 else 0
        r4 = d4.get("mpi_ranks", 0)
        r8 = d8.get("mpi_ranks", 0)
        rows.append(
            f"  {base} & {r4}/{r8} & {cd4:.6f}/{cd8:.6f} & {diff:.2e} & {rel:.3f}\\% & {w4:.1f}/{w8:.1f} & {speedup:.2f}x \\\\"
        )
    body = "\n".join(rows)
    return r"""\\begin{table}[htbp]
\\centering
\\caption{MPI rank-count comparison: np=4 vs np=8. $C_D$ difference, relative change, wall-clock time, and speedup.}
\\label{tab:mpi-scaling}
\\begin{tabular}{lccrrrl}
\\toprule
Case & Ranks & $C_D$ (4/8) & $\\Delta C_D$ & Rel.\\,Diff & Wall [s] (4/8) & Speedup \\\\
\\midrule
""" + body + r"""
\\bottomrule
\\end{tabular}
\\end{table}"""


# ---------------------------------------------------------------------------
# Figure generators
# ---------------------------------------------------------------------------

def plot_residual_history(case_id, data):
    """Plot residual L2 history on semilogy."""
    residuals = data.get("residuals", [])
    if not residuals:
        return None
    steps = [safe_float(r.get("step", i)) for i, r in enumerate(residuals)]
    l2 = [max(safe_float(r.get("residual_l2", 0)), 1e-30) for r in residuals]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.semilogy(steps, l2, linewidth=1.0, color="#1f77b4")
    ax.set_xlabel("Pseudo-time step")
    ax.set_ylabel(r"$\|R\|_{L_2}$ (global)")
    ax.set_title(f"Residual history: {case_id}")
    ax.grid(True, which="both", alpha=0.3)
    ax.ticklabel_format(style="sci", axis="x", scilimits=(0, 0))
    fig.tight_layout()
    path = FIGURES_DIR / f"{case_id}_residual.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def plot_force_history(case_id, data):
    """Plot CD and CL history."""
    forces = data.get("forces", [])
    if not forces:
        return None
    steps = [safe_float(r.get("step", i)) for i, r in enumerate(forces)]
    cd = [safe_float(r.get("cd", 0)) for r in forces]
    cl = [safe_float(r.get("cl", 0)) for r in forces]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(steps, cd, linewidth=1.0, color="#1f77b4", label=r"$C_D$")
    ax.plot(steps, cl, linewidth=1.0, color="#ff7f0e", label=r"$C_L$")
    ax.set_xlabel("Step" if data.get("final_time", 0) == 0 else "Physical time step")
    ax.set_ylabel("Force coefficient")
    ax.set_title(f"Force history: {case_id}")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = FIGURES_DIR / f"{case_id}_forces.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def plot_surface_cp(case_id, data):
    """Plot surface Cp distribution for NACA cases."""
    case_dir = RESULTS_DIR / case_id
    surface = load_csv(case_dir / "surface.csv")
    if not surface:
        return None
    x = np.array([safe_float(r.get("x", 0)) for r in surface])
    cp = np.array([safe_float(r.get("cp", 0)) for r in surface])
    # Sort by x for a cleaner curve
    sort_idx = np.argsort(x)
    x = x[sort_idx]
    cp = cp[sort_idx]
    fig, ax = plt.subplots(figsize=(7, 4.5))
    ax.plot(x, cp, linewidth=1.0, color="#1f77b4", marker=".", markersize=2, linestyle="-")
    ax.set_xlabel(r"$x/c$")
    ax.set_ylabel(r"$C_p$")
    ax.set_title(f"Surface pressure coefficient: {case_id}")
    ax.invert_yaxis()  # Cp convention
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    path = FIGURES_DIR / f"{case_id}_cp.png"
    fig.savefig(path, dpi=150)
    plt.close(fig)
    return path


def plot_field_contour(case_id, vtu_path, variable, clip_range=None):
    """Plot a filled contour from a VTU field file using matplotlib triangulation.

    variable: 'mach', 'pressure', 'velocity', or 'vorticity'
    """
    try:
        # Try pyvista first
        import pyvista as pv
        mesh = pv.read(str(vtu_path))
        pts = mesh.points[:, :2]
        cells = mesh.cells.reshape(-1, 3)[:, 1:] if mesh.n_cells > 0 else None
        # Fall back to manual
    except ImportError:
        pass

    # Parse VTU manually (simple XML parser for the data we need)
    try:
        import xml.etree.ElementTree as ET
        import struct
        import base64

        tree = ET.parse(str(vtu_path))
        root = tree.getroot()

        # Find points
        points_data = None
        for piece in root.iter("Piece"):
            npts = int(piece.get("NumberOfPoints", "0"))
            ncells = int(piece.get("NumberOfCells", "0"))

            pts_elem = piece.find(".//Points/DataArray")
            if pts_elem is not None:
                fmt = pts_elem.get("format", "ascii")
                if fmt == "ascii":
                    pts_raw = [float(x) for x in pts_elem.text.split()]
                    pts = np.array(pts_raw).reshape(-1, 3)[:, :2]
                else:
                    pts = np.zeros((npts, 2))

            # Find connectivity
            conn_elem = None
            for da in piece.iter("DataArray"):
                if da.get("Name") == "connectivity":
                    conn_elem = da
            cells_arr = []
            if conn_elem is not None and conn_elem.get("format") == "ascii":
                cells_arr = [int(x) for x in conn_elem.text.split()]

            # Find offsets
            off_elem = None
            for da in piece.iter("DataArray"):
                if da.get("Name") == "offsets":
                    off_elem = da
            offsets = []
            if off_elem is not None and off_elem.get("format") == "ascii":
                offsets = [int(x) for x in off_elem.text.split()]

            # Find the requested variable
            var_data = None
            for da in piece.iter("DataArray"):
                if da.get("Name", "").lower() == variable.lower():
                    if da.get("format") == "ascii":
                        var_data = np.array([float(x) for x in da.text.split()])
                    break

            # Also try cell data
            for cd in piece.iter("CellData"):
                for da in cd.iter("DataArray"):
                    if da.get("Name", "").lower() == variable.lower():
                        if da.get("format") == "ascii":
                            var_data = np.array([float(x) for x in da.text.split()])
                        break

            break

        if pts is None or var_data is None:
            return None

        # Build triangulation from cells
        if cells_arr and offsets:
            triangles = []
            start = 0
            for i, end in enumerate(offsets):
                n = end - start
                if n == 3:
                    triangles.append(cells_arr[start:end])
                elif n == 4:
                    # quad -> 2 triangles
                    q = cells_arr[start:end]
                    triangles.append([q[0], q[1], q[2]])
                    triangles.append([q[0], q[2], q[3]])
                start = end
            triangles = np.array(triangles) if triangles else None
        else:
            triangles = None

        if triangles is None or len(triangles) == 0:
            return None

        # var_data may be per-cell or per-point
        if len(var_data) == len(triangles):
            # Cell data -> convert to point data via averaging
            point_var = np.zeros(len(pts))
            count = np.zeros(len(pts))
            for i, tri in enumerate(triangles):
                for v in tri:
                    point_var[v] += var_data[i]
                    count[v] += 1
            point_var /= np.maximum(count, 1)
        else:
            point_var = var_data[:len(pts)]

        fig, ax = plt.subplots(figsize=(8, 6))

        if clip_range:
            vmin, vmax = clip_range
        else:
            # Use 2nd/98th percentile to avoid outlier collapse
            vmin = np.percentile(point_var, 2)
            vmax = np.percentile(point_var, 98)

        tcf = ax.tripcolor(pts[:, 0], pts[:, 1], triangles,
                           facecolors=point_var if len(point_var) == len(triangles) else None,
                           shading="gouraud" if len(point_var) == len(pts) else "flat",
                           cmap="jet", vmin=vmin, vmax=vmax)

        cbar = fig.colorbar(tcf, ax=ax, shrink=0.8)
        var_label = {"mach": "Mach", "pressure": "Pressure", "velocity": "|V|", "vorticity": "Vorticity"}
        cbar.set_label(var_label.get(variable, variable))
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        ax.set_title(f"{var_label.get(variable, variable)} contour: {case_id}")
        ax.set_aspect("equal")
        fig.tight_layout()
        path = FIGURES_DIR / f"{case_id}_{variable}.png"
        fig.savefig(path, dpi=150)
        plt.close(fig)
        return path

    except Exception as e:
        print(f"[generate_report] Warning: could not plot {case_id} {variable}: {e}", file=sys.stderr)
        return None


def plot_vorticity_wake(case_id, data):
    """Plot vorticity contour clipped to [-5, 5] for the Re200 case."""
    case_dir = RESULTS_DIR / case_id
    vtu_path = case_dir / "field_final.vtu"
    return plot_field_contour(case_id, vtu_path, "vorticity", clip_range=[-5, 5])


def generate_all_figures(all_data):
    """Generate all required figures and return a manifest."""
    figure_manifest = []

    for cid in REQUIRED_CASES + ["naca0012_m015_inviscid_np8", "cylinder_m010_laminar_re20_np8"]:
        d = all_data.get(cid, {})
        if not d.get("exists", False):
            continue

        # Residual history
        p = plot_residual_history(cid, d)
        if p:
            figure_manifest.append({
                "figure_file": str(p.relative_to(REPORT_DIR)),
                "case_id": cid,
                "figure_type": "residual_history",
                "variable": "residual_l2",
                "source_file": str((RESULTS_DIR / cid / "residuals.csv").relative_to(REPO_ROOT)),
                "caption": f"Residual L2 norm history for {cid}.",
            })

        # Force history
        p = plot_force_history(cid, d)
        if p:
            figure_manifest.append({
                "figure_file": str(p.relative_to(REPORT_DIR)),
                "case_id": cid,
                "figure_type": "force_history",
                "variable": "cd_cl",
                "source_file": str((RESULTS_DIR / cid / "forces.csv").relative_to(REPO_ROOT)),
                "caption": f"Drag and lift coefficient history for {cid}.",
            })

        # Surface Cp (NACA and cylinder)
        p = plot_surface_cp(cid, d)
        if p:
            figure_manifest.append({
                "figure_file": str(p.relative_to(REPORT_DIR)),
                "case_id": cid,
                "figure_type": "surface_cp",
                "variable": "cp",
                "source_file": str((RESULTS_DIR / cid / "surface.csv").relative_to(REPO_ROOT)),
                "caption": f"Surface pressure coefficient distribution for {cid}.",
            })

        # Field contours: mach and pressure
        vtu_path = RESULTS_DIR / cid / "field_final.vtu"
        if vtu_path.exists():
            p = plot_field_contour(cid, vtu_path, "mach")
            if p:
                figure_manifest.append({
                    "figure_file": str(p.relative_to(REPORT_DIR)),
                    "case_id": cid,
                    "figure_type": "field_contour",
                    "variable": "mach",
                    "source_file": str(vtu_path.relative_to(REPO_ROOT)),
                    "caption": f"Mach number contour for {cid}.",
                })
            p = plot_field_contour(cid, vtu_path, "pressure")
            if p:
                figure_manifest.append({
                    "figure_file": str(p.relative_to(REPORT_DIR)),
                    "case_id": cid,
                    "figure_type": "field_contour",
                    "variable": "pressure",
                    "source_file": str(vtu_path.relative_to(REPO_ROOT)),
                    "caption": f"Pressure contour for {cid}.",
                })

        # Vorticity wake for Re200
        if cid == "cylinder_m010_laminar_re200":
            p = plot_vorticity_wake(cid, d)
            if p:
                figure_manifest.append({
                    "figure_file": str(p.relative_to(REPORT_DIR)),
                    "case_id": cid,
                    "figure_type": "vorticity_wake",
                    "variable": "vorticity",
                    "source_file": str(vtu_path.relative_to(REPO_ROOT)),
                    "caption": f"Vorticity contour (clipped to [-5, 5]) showing vortex street for {cid}.",
                })

    return figure_manifest


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------

def run_sanity_checks(all_data):
    """Run physics sanity checks and return a dict for JSON output."""
    checks = {}

    for cid in REQUIRED_CASES:
        d = all_data.get(cid, {})
        case_checks = {"passed": True, "checks": []}

        if not d.get("exists", False):
            case_checks["passed"] = False
            case_checks["checks"].append({"name": "case_exists", "passed": False, "detail": "Result directory not found"})
            checks[cid] = case_checks
            continue

        case_dir = RESULTS_DIR / cid

        # 1. Positive density and pressure in field
        surface = load_csv(case_dir / "surface.csv")
        has_positive = True
        if surface:
            for r in surface[:100]:  # check first 100 rows
                rho = safe_float(r.get("rho", 0))
                p = safe_float(r.get("pressure", 0))
                if rho <= 0 or p <= 0:
                    has_positive = False
                    break
        case_checks["checks"].append({
            "name": "positive_density_pressure",
            "passed": has_positive,
            "detail": "Surface pressure and density must be positive" if has_positive else "Non-positive density or pressure found",
        })

        # 2. NACA at AoA=0: near-zero lift
        meta = CASE_META.get(cid, {})
        if meta.get("geometry") == "NACA0012":
            cl = abs(d.get("final_cl", 999))
            near_zero = cl < 0.1
            case_checks["checks"].append({
                "name": "symmetry_zero_lift",
                "passed": near_zero,
                "detail": f"|C_L| = {cl:.6e} (should be near zero at AoA=0)" if near_zero else f"|C_L| = {cl:.6e} exceeds tolerance",
            })

        # 3. Cylinder: positive mean drag
        if meta.get("geometry") == "Cylinder":
            cd = d.get("final_cd", 0) if cid != "cylinder_m010_laminar_re200" else d.get("mean_cd", 0)
            positive_drag = cd > 0
            case_checks["checks"].append({
                "name": "positive_drag",
                "passed": positive_drag,
                "detail": f"C_D = {cd:.6e} (must be positive)" if positive_drag else f"C_D = {cd:.6e} is non-positive",
            })

        # 4. Re200: nonzero unsteady lift
        if cid == "cylinder_m010_laminar_re200":
            forces = d.get("forces", [])
            if len(forces) > 100:
                cls = [safe_float(r.get("cl", 0)) for r in forces[-len(forces)//2:]]
                cl_range = max(cls) - min(cls) if cls else 0
                unsteady = cl_range > 0.01
                case_checks["checks"].append({
                    "name": "unsteady_lift_variation",
                    "passed": unsteady,
                    "detail": f"C_L range = {cl_range:.6e} (post-transient shedding)" if unsteady else f"C_L range = {cl_range:.6e} (no shedding detected)",
                })

        # 5. Cp varies along body
        if surface:
            cps = [safe_float(r.get("cp", 0)) for r in surface]
            cp_range = max(cps) - min(cps) if cps else 0
            cp_varies = cp_range > 0.01
            case_checks["checks"].append({
                "name": "cp_variation",
                "passed": cp_varies,
                "detail": f"C_p range = {cp_range:.4f}" if cp_varies else f"C_p range = {cp_range:.4f} (too small)",
            })

        # 7. No-slip wall: near-zero velocity
        if meta.get("mode") == "laminar":
            if surface:
                wall_vels = []
                for r in surface:
                    u = safe_float(r.get("u", 0))
                    v = safe_float(r.get("v", 0))
                    wall_vels.append(math.sqrt(u**2 + v**2))
                max_wall_vel = max(wall_vels) if wall_vels else 999
                near_zero_vel = max_wall_vel < 0.1
                case_checks["checks"].append({
                    "name": "no_slip_wall_velocity",
                    "passed": near_zero_vel,
                    "detail": f"max |V_wall| = {max_wall_vel:.6e}" if near_zero_vel else f"max |V_wall| = {max_wall_vel:.6e} (should be near zero)",
                })

        # 8. Inviscid: near-zero viscous force
        if meta.get("mode") == "inviscid":
            vd = abs(d.get("viscous_drag", 0))
            no_visc = vd < 1e-10
            case_checks["checks"].append({
                "name": "inviscid_zero_viscous_force",
                "passed": no_visc,
                "detail": f"viscous_drag = {vd:.2e}" if no_visc else f"viscous_drag = {vd:.2e} (should be zero for inviscid)",
            })

        # Overall pass
        case_checks["passed"] = all(c["passed"] for c in case_checks["checks"])
        checks[cid] = case_checks

    # Summary
    all_pass = all(checks.get(c, {}).get("passed", False) for c in REQUIRED_CASES)
    result = {
        "overall_passed": all_pass,
        "cases": checks,
    }
    return result


# ---------------------------------------------------------------------------
# Run manifest
# ---------------------------------------------------------------------------

def generate_run_manifest(all_data):
    """Generate run_manifest.csv."""
    rows = []
    for cid in REQUIRED_CASES:
        d = all_data.get(cid, {})
        rs = d.get("run_status", {})
        cmd = rs.get("command", "")
        rows.append({
            "case_id": cid,
            "command": cmd,
            "mpi_ranks": d.get("mpi_ranks", 0),
            "wall_time": f"{d.get('wall_time', 0):.2f}",
            "convergence_status": d.get("convergence_status", "missing"),
            "final_cd": f"{d.get('final_cd', 0):.6e}",
            "final_cl": f"{d.get('final_cl', 0):.6e}",
        })
    # Add scaling runs
    for base, scaled in SCALING_PAIRS:
        d = all_data.get(scaled, {})
        if d.get("exists", False):
            rs = d.get("run_status", {})
            rows.append({
                "case_id": scaled,
                "command": rs.get("command", ""),
                "mpi_ranks": d.get("mpi_ranks", 0),
                "wall_time": f"{d.get('wall_time', 0):.2f}",
                "convergence_status": d.get("convergence_status", "missing"),
                "final_cd": f"{d.get('final_cd', 0):.6e}",
                "final_cl": f"{d.get('final_cl', 0):.6e}",
            })

    manifest_path = REPORT_DIR / "run_manifest.csv"
    with open(manifest_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["case_id", "command", "mpi_ranks",
                                                "wall_time", "convergence_status",
                                                "final_cd", "final_cl"])
        writer.writeheader()
        writer.writerows(rows)
    return manifest_path


# ---------------------------------------------------------------------------
# Figure manifest
# ---------------------------------------------------------------------------

def write_figure_manifest(figure_manifest):
    """Write figure_manifest.csv."""
    path = REPORT_DIR / "figure_manifest.csv"
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["figure_file", "case_id", "figure_type",
                                                "variable", "source_file", "caption"])
        writer.writeheader()
        writer.writerows(figure_manifest)
    return path


# ---------------------------------------------------------------------------
# Partition table
# ---------------------------------------------------------------------------

def gen_partition_table(all_data):
    """Generate a partition diagnostics table from a representative run."""
    # Use naca0012_m015_inviscid (np=4) as representative
    d = all_data.get("naca0012_m015_inviscid", {})
    pd = d.get("partition", {})
    ranks = pd.get("ranks", [])

    if not ranks:
        return "% No partition data available"

    rows = []
    for r in ranks:
        rows.append(
            f"  {r.get('rank', 0)} & {r.get('num_cells_owned', 0)} & {r.get('num_cells_ghost', 0)} & "
            f"{r.get('num_boundary_faces', 0)} & {r.get('num_neighbor_ranks', 0)} & "
            f"{r.get('send_cells', 0)} & {r.get('recv_cells', 0)} \\\\"
        )
    body = "\n".join(rows)

    lb = pd.get("load_balance_ratio", 0)
    ec = pd.get("edge_cut", 0)

    return f"""\\begin{{table}}[htbp]
\\centering
\\caption{{Per-rank partition diagnostics for the NACA0012 mesh at np=4 (METIS k-way). Edge cut = {ec}, load-balance ratio = {lb:.4f}.}}
\\label{{tab:partition}}
\\begin{{tabular}}{{rrrrrrr}}
\\toprule
Rank & Owned & Ghost & Bnd.\\,Faces & Neighbors & Send & Recv \\\\
\\midrule
{body}
\\bottomrule
\\end{{tabular}}
\\end{{table}}"""


# ---------------------------------------------------------------------------
# Results text generation
# ---------------------------------------------------------------------------

def gen_results_text(all_data):
    """Generate per-case result discussion text."""
    sections = []
    for cid in REQUIRED_CASES:
        d = all_data.get(cid, {})
        meta = CASE_META.get(cid, {})

        cd = d.get("final_cd", 0)
        cl = d.get("final_cl", 0)
        cmz = d.get("final_cmz", 0)
        res_red = d.get("residual_reduction", 0)
        status = d.get("convergence_status", "missing")
        wall = d.get("wall_time", 0)
        ranks = d.get("mpi_ranks", 0)
        pd = d.get("viscous_drag", 0)
        vd = d.get("pressure_drag", 0)  # note: variable name

        text = f"\\paragraph*{{{cid}}}\n"
        text += f"This case was run at Mach {meta.get('mach', '?')} in {meta.get('mode', '?')} mode "
        if meta.get("re", "-") != "-":
            text += f"at Re={meta.get('re')}. "
        else:
            text += ". "
        text += f"It used {ranks} MPI ranks and completed in {wall:.1f} seconds. "
        text += f"The residual was reduced by {res_red:.2f} orders of magnitude. "
        text += f"Final force coefficients: $C_D={cd:.6f}$, $C_L={cl:.6f}$, $C_m={cmz:.6f}$. "
        text += f"Drag split: pressure $C_{{D,p}}={d.get('pressure_drag',0):.6f}$, viscous $C_{{D,v}}={d.get('viscous_drag',0):.6f}$. "
        text += f"Convergence status: \\textbf{{{status}}}.\n"

        if cid == "cylinder_m010_laminar_re200":
            mean_cd = d.get("mean_cd", 0)
            cl_amp = d.get("cl_amplitude", 0)
            st = d.get("strouhal", None)
            freq = d.get("shedding_freq", None)
            text += f"\nPost-transient analysis: mean $C_D={mean_cd:.4f}$, "
            text += f"$C_L$ amplitude $= {cl_amp:.4f}$. "
            if st is not None:
                text += f"Estimated shedding frequency $f={freq:.4f}$, "
                text += f"Strouhal number $St={st:.4f}$. "
            else:
                text += "Shedding frequency could not be reliably estimated. "

        sections.append(text)

    return "\n\n".join(sections)


# ---------------------------------------------------------------------------
# Report update via placeholder replacement
# ---------------------------------------------------------------------------

def update_report_tex(all_data, figure_manifest):
    """Replace placeholders in report.tex with generated content."""
    if not REPORT_TEX.exists():
        print(f"[generate_report] Warning: {REPORT_TEX} not found, skipping update.", file=sys.stderr)
        return

    with open(REPORT_TEX, "r") as f:
        content = f.read()

    replacements = {
        "%%PLACEHOLDER:RUN_STATUS_TABLE%%": gen_run_status_table(all_data),
        "%%PLACEHOLDER:FORCE_TABLE%%": gen_force_table(all_data),
        "%%PLACEHOLDER:MPI_TABLE%%": gen_mpi_table(all_data),
        "%%PLACEHOLDER:PARTITION_TABLE%%": gen_partition_table(all_data),
        "%%PLACEHOLDER:RESULTS_TEXT%%": gen_results_text(all_data),
        "%%PLACEHOLDER:FIGURE_MANIFEST%%": gen_figure_manifest_latex(figure_manifest),
    }

    for placeholder, replacement in replacements.items():
        content = content.replace(placeholder, replacement)

    with open(REPORT_TEX, "w") as f:
        f.write(content)


def gen_figure_manifest_latex(figure_manifest):
    """Generate a LaTeX table listing all figures and their sources."""
    if not figure_manifest:
        return "% No figures generated"

    rows = []
    for fm in figure_manifest:
        fname = os.path.basename(fm["figure_file"])
        rows.append(
            f"  {fname} & {fm['case_id']} & {fm['figure_type']} & {fm['variable']} & {os.path.basename(fm['source_file'])} \\\\"
        )
    body = "\n".join(rows)

    return f"""\\begin{{table}}[htbp]
\\centering
\\caption{{Figure manifest mapping each figure to its source data file and plotted variable.}}
\\label{{tab:figure-manifest}}
\\begin{{tabular}}{{lllll}}
\\toprule
Figure & Case & Type & Variable & Source \\\\
\\midrule
{body}
\\bottomrule
\\end{{tabular}}
\\end{{table}}"""


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("[generate_report] Scanning results directory...")

    # Collect data for all cases
    all_data = {}
    all_case_ids = REQUIRED_CASES + ["naca0012_m015_inviscid_np8", "cylinder_m010_laminar_re20_np8"]
    for cid in all_case_ids:
        case_dir = RESULTS_DIR / cid
        all_data[cid] = extract_case_data(cid, case_dir)
        status = all_data[cid]["convergence_status"]
        print(f"  {cid}: {status} (CD={all_data[cid]['final_cd']:.4e}, CL={all_data[cid]['final_cl']:.4e})")

    # Generate run manifest
    manifest_path = generate_run_manifest(all_data)
    print(f"[generate_report] Run manifest: {manifest_path}")

    # Generate figures
    print("[generate_report] Generating figures...")
    figure_manifest = generate_all_figures(all_data)
    fm_path = write_figure_manifest(figure_manifest)
    print(f"[generate_report] Figure manifest: {fm_path} ({len(figure_manifest)} figures)")

    # Run sanity checks
    print("[generate_report] Running sanity checks...")
    sanity = run_sanity_checks(all_data)
    sanity_path = REPORT_DIR / "sanity_checks.json"
    with open(sanity_path, "w") as f:
        json.dump(sanity, f, indent=2)
    print(f"[generate_report] Sanity checks: {sanity_path} (overall: {'PASS' if sanity['overall_passed'] else 'FAIL'})")
    for cid in REQUIRED_CASES:
        cs = sanity.get("cases", {}).get(cid, {})
        print(f"  {cid}: {'PASS' if cs.get('passed') else 'FAIL'}")

    # Update report.tex with actual data
    print("[generate_report] Updating report.tex with generated data...")
    update_report_tex(all_data, figure_manifest)

    print("[generate_report] Done.")


if __name__ == "__main__":
    main()

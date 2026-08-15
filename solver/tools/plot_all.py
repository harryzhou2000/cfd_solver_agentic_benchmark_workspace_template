#!/usr/bin/env python3
"""
Comprehensive post-processing and visualization script for CFD benchmark.

Generates all required figures for the Phase 4 report:
  - mach.png, pressure.png (field contours from VTU)
  - residuals.png (residual history from residuals.csv)
  - forces.png (force history from forces.csv)
  - cp_surface.png (surface Cp or wall pressure/friction from surface.csv)
  - wake_vorticity.png (cylinder Re200 wake visualization)

Usage:
    python tools/plot_all.py --results-dir <path> --output-dir <path> --case-id <id>
"""

import argparse
import os
import sys

import numpy as np

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.tri import Triangulation
except ImportError:
    print("matplotlib not installed. Run setup_venv.sh first.", file=sys.stderr)
    sys.exit(1)

try:
    import meshio
    HAS_MESHIO = True
except ImportError:
    HAS_MESHIO = False
    print("Warning: meshio not installed. Field contour plots will be skipped.", file=sys.stderr)


# ── Styling ──────────────────────────────────────────────────────────────
plt.rcParams.update({
    "font.size": 12,
    "axes.labelsize": 13,
    "axes.titlesize": 14,
    "legend.fontsize": 10,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "figure.dpi": 150,
    "savefig.dpi": 150,
    "savefig.bbox": "tight",
    "image.cmap": "viridis",
})


CASE_LABELS = {
    "naca0012_m015_inviscid": "NACA0012, M=0.15, Inviscid",
    "naca0012_m080_inviscid": "NACA0012, M=0.8, Inviscid",
    "naca0012_m200_inviscid": "NACA0012, M=2.0, Inviscid",
    "naca0012_m015_laminar_re5000": "NACA0012, M=0.15, Re=5000",
    "naca0012_m080_laminar_re5000": "NACA0012, M=0.8, Re=5000",
    "naca0012_m200_laminar_re5000": "NACA0012, M=2.0, Re=5000",
    "cylinder_m010_laminar_re20": "Cylinder, M=0.1, Re=20",
    "cylinder_m010_laminar_re200": "Cylinder, M=0.1, Re=200",
}


def is_cylinder_case(case_id):
    return "cylinder" in case_id


def is_naca_case(case_id):
    return "naca" in case_id


# ═════════════════════════════════════════════════════════════════════════
# VTU Field Reader
# ═════════════════════════════════════════════════════════════════════════

def read_vtu_field(vtu_path, field_name):
    """Read a cell-centered field from a VTU file using meshio.
    Returns (x_nodes, y_nodes, triangles, node_values) for tricontourf.
    Node values are computed by averaging cell-centered values to nodes.
    """
    if not HAS_MESHIO:
        return None, None, None, None

    if not os.path.exists(vtu_path):
        print(f"VTU file not found: {vtu_path}", file=sys.stderr)
        return None, None, None, None

    mesh = meshio.read(vtu_path)
    points = mesh.points
    n_nodes = points.shape[0]
    x = points[:, 0]
    y = points[:, 1]

    if field_name not in mesh.cell_data:
        available = list(mesh.cell_data.keys())
        print(f"Field '{field_name}' not found. Available: {available}", file=sys.stderr)
        return None, None, None, None

    raw_field = mesh.cell_data[field_name]
    if not isinstance(raw_field, list):
        raw_field = [raw_field]

    # Step 1: Build triangulation (split quads/polygons to triangles)
    # and simultaneously accumulate node contributions
    all_triangles = []
    node_sums = np.zeros(n_nodes)
    node_counts = np.zeros(n_nodes, dtype=int)

    for iblock, block in enumerate(mesh.cells):
        cell_type = block.type
        connectivity = block.data
        n_cells = connectivity.shape[0]
        field_block = np.asarray(raw_field[iblock]).ravel()[:n_cells]

        for ci in range(n_cells):
            cell_nodes = connectivity[ci]
            nv = len(cell_nodes)
            val = float(field_block[ci]) if ci < len(field_block) else 0.0
            if not np.isfinite(val):
                val = 0.0

            # Accumulate to nodes
            for ni in cell_nodes:
                if ni < n_nodes:
                    node_sums[ni] += val
                    node_counts[ni] += 1

            # Triangulate
            if nv == 3:
                all_triangles.append(cell_nodes)
            elif nv == 4:
                all_triangles.append([cell_nodes[0], cell_nodes[1], cell_nodes[2]])
                all_triangles.append([cell_nodes[0], cell_nodes[2], cell_nodes[3]])
            elif nv >= 5:
                for j in range(1, nv - 1):
                    all_triangles.append([cell_nodes[0], cell_nodes[j], cell_nodes[j + 1]])

    if not all_triangles:
        print("No cells found in VTU file", file=sys.stderr)
        return None, None, None, None

    triangles = np.array(all_triangles, dtype=int)

    # Step 2: Node-averaged values
    mask = node_counts > 0
    node_vals = np.zeros(n_nodes)
    node_vals[mask] = node_sums[mask] / node_counts[mask]

    return x, y, triangles, node_vals


def plot_field_contour(vtu_path, field_name, output_path, case_label,
                        cmap=None, vmin=None, vmax=None, clip_percentile=None,
                        xlim=None, ylim=None, zoom_label=""):
    """Generate a filled contour plot of a cell-centered field."""
    x, y, triangles, vals = read_vtu_field(vtu_path, field_name)
    if x is None:
        return False

    if clip_percentile is not None:
        lo = np.percentile(vals, clip_percentile[0])
        hi = np.percentile(vals, clip_percentile[1])
        vals = np.clip(vals, lo, hi)
        if vmin is None:
            vmin = lo
        if vmax is None:
            vmax = hi

    if cmap is None:
        cmap = "viridis"

    fig, ax = plt.subplots(figsize=(10, 7))
    tri = Triangulation(x, y, triangles)
    tcf = ax.tricontourf(tri, vals, levels=50, cmap=cmap, vmin=vmin, vmax=vmax)
    cbar = fig.colorbar(tcf, ax=ax, shrink=0.8)
    cbar.set_label(field_name.capitalize())

    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    title = f"{case_label}\n{field_name.capitalize()} field"
    if zoom_label:
        title += f" ({zoom_label})"
    ax.set_title(title)

    if xlim is not None:
        ax.set_xlim(xlim)
    if ylim is not None:
        ax.set_ylim(ylim)

    fig.tight_layout()
    fig.savefig(output_path)
    plt.close(fig)
    print(f"Saved {output_path}")
    return True


# ═════════════════════════════════════════════════════════════════════════
# CSV Line Plots
# ═════════════════════════════════════════════════════════════════════════

def plot_residuals(results_dir, output_dir, case_label, case_id):
    """Plot residual history from residuals.csv."""
    path = os.path.join(results_dir, "residuals.csv")
    if not os.path.exists(path):
        print(f"Skipping residuals: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping residuals: empty data", file=sys.stderr)
        return

    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    # Component residuals
    ax = axes[0]
    for comp, lbl in [("rho", r"$\rho$"), ("rhou", r"$\rho u$"),
                       ("rhov", r"$\rho v$"), ("rhoE", r"$\rho E$")]:
        if comp in data.dtype.names:
            ax.semilogy(data["step"], np.abs(data[comp]), label=lbl, linewidth=0.8)
    ax.set_ylabel("Component Residual (L2)")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)

    # Overall residuals
    ax = axes[1]
    if "residual_l2" in data.dtype.names:
        ax.semilogy(data["step"], data["residual_l2"], label="L2 norm", linewidth=0.8)
    if "residual_linf" in data.dtype.names:
        ax.semilogy(data["step"], data["residual_linf"], label=r"$L_\infty$ norm", linewidth=0.8)
    ax.set_xlabel("Step")
    ax.set_ylabel("Residual Norm")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    fig.suptitle(f"Residual History — {case_label}")
    fig.tight_layout()
    fig.savefig(os.path.join(output_dir, f"{case_id}_residuals.png"))
    plt.close(fig)
    print(f"Saved residuals plot")


def plot_forces(results_dir, output_dir, case_label, case_id):
    """Plot force history from forces.csv."""
    path = os.path.join(results_dir, "forces.csv")
    if not os.path.exists(path):
        print(f"Skipping forces: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping forces: empty data", file=sys.stderr)
        return

    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    x_axis = data["step"]
    # For transient cases, use physical_time if available
    if "physical_time" in data.dtype.names and np.max(data["physical_time"]) > 0:
        # Check which axis makes sense for the plot
        pass

    ax = axes[0]
    for key, lbl in [("cl", "$C_L$"), ("cd", "$C_D$")]:
        if key in data.dtype.names:
            ax.plot(x_axis, data[key], label=lbl, linewidth=0.8)
    ax.set_ylabel("Force Coefficient")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    ax = axes[1]
    for key, lbl in [("pressure_drag", "P-Drag"), ("viscous_drag", "V-Drag"),
                     ("pressure_lift", "P-Lift"), ("viscous_lift", "V-Lift")]:
        if key in data.dtype.names:
            ax.plot(x_axis, data[key], label=lbl, linewidth=0.8)
    ax.set_xlabel("Step")
    ax.set_ylabel("Force Components")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)

    fig.suptitle(f"Force History — {case_label}")
    fig.tight_layout()
    fig.savefig(os.path.join(output_dir, f"{case_id}_forces.png"))
    plt.close(fig)
    print(f"Saved forces plot")


def plot_cp_surface(results_dir, output_dir, case_label, case_id, prefix=""):
    prefix = prefix if prefix else case_id
    """Plot surface Cp or wall pressure/friction from surface.csv."""
    path = os.path.join(results_dir, "surface.csv")
    if not os.path.exists(path):
        print(f"Skipping Cp/surface: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping Cp/surface: empty data", file=sys.stderr)
        return

    if is_naca_case(case_id):
        # NACA: Cp vs x (or chord position)
        fig, ax = plt.subplots(figsize=(8, 5))
        x = data["x"]
        cp = data["cp"]
        # Invert Cp axis (aerodynamics convention)
        ax.scatter(x, cp, s=4, alpha=0.7, c="steelblue", edgecolors="none")
        ax.set_xlabel("x (chord direction)")
        ax.set_ylabel("$C_p$")
        ax.invert_yaxis()
        ax.grid(True, alpha=0.3)
        ax.set_title(f"Surface Pressure Coefficient — {case_label}")
        fig.tight_layout()
        fig.savefig(os.path.join(output_dir, f"{prefix}_cp_surface.png"))
        plt.close(fig)

    elif is_cylinder_case(case_id):
        # Cylinder: Cp and Cf vs angle
        fig, axes = plt.subplots(2, 1, figsize=(8, 8), sharex=True)

        x = data["x"]
        y = data["y"]
        theta = np.arctan2(y, x) * 180.0 / np.pi  # angle in degrees

        ax = axes[0]
        ax.scatter(theta, data["cp"], s=4, alpha=0.7, c="steelblue", edgecolors="none")
        ax.set_ylabel("$C_p$")
        ax.invert_yaxis()
        ax.grid(True, alpha=0.3)

        ax = axes[1]
        ax.scatter(theta, data["cf"], s=4, alpha=0.7, c="firebrick", edgecolors="none")
        ax.set_ylabel("$C_f$")
        ax.set_xlabel(r"$\theta$ (degrees)")
        ax.grid(True, alpha=0.3)

        fig.suptitle(f"Wall Pressure and Skin Friction — {case_label}")
        fig.tight_layout()
        fig.savefig(os.path.join(output_dir, f"{prefix}_cp_surface.png"))
        plt.close(fig)

    print(f"Saved Cp/surface plot")


def plot_wake_vorticity(results_dir, output_dir, case_label):
    """Plot wake visualization for cylinder Re200 — velocity magnitude field."""
    vtu_path = os.path.join(results_dir, "field_final.vtu")
    if not os.path.exists(vtu_path):
        print(f"VTU not found for wake plot: {vtu_path}", file=sys.stderr)
        return

    # Compute velocity magnitude from velocity components
    x, y, triangles, _ = read_vtu_field(vtu_path, "density")
    if x is None:
        return

    # Re-read velocity
    if not HAS_MESHIO:
        return

    mesh = meshio.read(vtu_path)
    if "velocity" in mesh.cell_data:
        vel = mesh.cell_data["velocity"]
        if len(vel.shape) > 1:
            vel_mag = np.sqrt(vel[:, 0]**2 + vel[:, 1]**2)
        else:
            vel_mag = np.abs(vel)
    elif "mach" in mesh.cell_data:
        vel_mag = mesh.cell_data["mach"]
        print("Using mach as proxy for wake visualization")
    else:
        print("No velocity or mach field for wake", file=sys.stderr)
        return

    fig, ax = plt.subplots(figsize=(12, 6))
    tri = Triangulation(x, y, triangles)
    tcf = ax.tricontourf(tri, vel_mag, levels=80, cmap="inferno",
                          vmin=0.0, vmax=np.percentile(vel_mag, 99))
    cbar = fig.colorbar(tcf, ax=ax, shrink=0.8)
    cbar.set_label("Velocity Magnitude")

    # Zoom to wake region
    ax.set_xlim(-2, 15)
    ax.set_ylim(-5, 5)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(f"Wake Velocity Magnitude — {case_label}")

    fig.tight_layout()
    fig.savefig(os.path.join(output_dir, "wake_vorticity.png"))
    plt.close(fig)
    print(f"Saved wake vorticity plot")


# ═════════════════════════════════════════════════════════════════════════
# Main
# ═════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="Generate all CFD benchmark figures")
    parser.add_argument("--results-dir", required=True, help="Case results directory")
    parser.add_argument("--output-dir", required=True, help="Figure output directory")
    parser.add_argument("--case-id", required=True, help="Case identifier")
    parser.add_argument("--skip-fields", action="store_true",
                        help="Skip VTU field contour plots")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    case_label = CASE_LABELS.get(args.case_id, args.case_id)
    results_dir = args.results_dir
    output_dir = args.output_dir

    vtu_path = os.path.join(results_dir, "field_final.vtu")

    cid = args.case_id  # shorthand

    # ── CSV-based plots (always) ──
    plot_residuals(results_dir, output_dir, case_label, cid)
    plot_forces(results_dir, output_dir, case_label, cid)
    plot_cp_surface(results_dir, output_dir, case_label, cid)

    # ── Field contour plots (skip if no VTU) ──
    if not args.skip_fields and os.path.exists(vtu_path):
        # Mach contour (full domain)
        plot_field_contour(
            vtu_path, "mach",
            os.path.join(output_dir, f"{cid}_mach.png"),
            case_label, cmap="coolwarm",
            vmin=0.0, vmax=None, clip_percentile=[0.5, 99.5])

        # Mach contour (near-body zoom)
        if is_naca_case(cid):
            plot_field_contour(
                vtu_path, "mach",
                os.path.join(output_dir, f"{cid}_mach_zoom.png"),
                case_label, cmap="coolwarm",
                vmin=0.0, vmax=None, clip_percentile=[0.5, 99.5],
                xlim=(-0.5, 1.5), ylim=(-0.5, 0.5), zoom_label="near-body")

        # Pressure contour
        plot_field_contour(
            vtu_path, "pressure",
            os.path.join(output_dir, f"{cid}_pressure.png"),
            case_label, cmap="plasma",
            clip_percentile=[0.5, 99.5])

        if is_naca_case(cid):
            plot_field_contour(
                vtu_path, "pressure",
                os.path.join(output_dir, f"{cid}_pressure_zoom.png"),
                case_label, cmap="plasma",
                clip_percentile=[0.5, 99.5],
                xlim=(-0.5, 1.5), ylim=(-0.5, 0.5), zoom_label="near-body")

        # Wake plot for cylinder Re200
        if cid == "cylinder_m010_laminar_re200":
            plot_wake_vorticity(results_dir, output_dir, case_label)

    print(f"All plots for {args.case_id} complete.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Post-processing script for the CFD solver.

Reads residuals.csv, forces.csv, and surface.csv from the results directory
and generates plots in the output directory.

Usage:
    python tools/plot_results.py --results-dir <path> --output-dir <path>
"""

import argparse
import os
import sys

import numpy as np

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    print("matplotlib not installed. Run tools/setup_venv.sh first.", file=sys.stderr)
    sys.exit(1)


def plot_residuals(results_dir, output_dir):
    """Plot residual history from residuals.csv."""
    path = os.path.join(results_dir, "residuals.csv")
    if not os.path.exists(path):
        print(f"Skipping residuals plot: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping residuals plot: empty data in {path}", file=sys.stderr)
        return

    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    # Component residuals
    ax = axes[0]
    for comp in ["rho", "rhou", "rhov", "rhoE"]:
        if comp in data.dtype.names:
            ax.semilogy(data["step"], data[comp], label=comp, linewidth=0.8)
    ax.set_ylabel("Component Residual (L2)")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # Overall residuals
    ax = axes[1]
    if "residual_l2" in data.dtype.names:
        ax.semilogy(data["step"], data["residual_l2"], label="L2", linewidth=0.8)
    if "residual_linf" in data.dtype.names:
        ax.semilogy(data["step"], data["residual_linf"], label="Linf", linewidth=0.8)
    ax.set_xlabel("Step")
    ax.set_ylabel("Residual Norm")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    fig.suptitle("Residual History")
    fig.tight_layout()

    out_path = os.path.join(output_dir, "residuals.png")
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"Saved residuals plot to {out_path}")


def plot_forces(results_dir, output_dir):
    """Plot force history from forces.csv."""
    path = os.path.join(results_dir, "forces.csv")
    if not os.path.exists(path):
        print(f"Skipping forces plot: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping forces plot: empty data in {path}", file=sys.stderr)
        return

    fig, axes = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    # CL and CD
    ax = axes[0]
    for key, lbl in [("cl", "CL"), ("cd", "CD")]:
        if key in data.dtype.names:
            ax.plot(data["step"], data[key], label=lbl, linewidth=0.8)
    ax.set_ylabel("Force Coefficient")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # Drag/lift breakdown
    ax = axes[1]
    for key, lbl in [("pressure_drag", "P-Drag"), ("viscous_drag", "V-Drag"),
                     ("pressure_lift", "P-Lift"), ("viscous_lift", "V-Lift")]:
        if key in data.dtype.names:
            ax.plot(data["step"], data[key], label=lbl, linewidth=0.8)
    ax.set_xlabel("Step")
    ax.set_ylabel("Force Components")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    fig.suptitle("Force History")
    fig.tight_layout()

    out_path = os.path.join(output_dir, "forces.png")
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"Saved forces plot to {out_path}")


def plot_cp(results_dir, output_dir):
    """Plot Cp distribution from surface.csv."""
    path = os.path.join(results_dir, "surface.csv")
    if not os.path.exists(path):
        print(f"Skipping Cp plot: {path} not found", file=sys.stderr)
        return

    data = np.genfromtxt(path, delimiter=",", names=True, deletechars="")
    if data is None or len(data) == 0:
        print(f"Skipping Cp plot: empty data in {path}", file=sys.stderr)
        return

    fig, ax = plt.subplots(figsize=(8, 5))

    x = data["x"]
    cp = data["cp"]

    ax.plot(x, cp, "o", markersize=1.5, alpha=0.7)
    ax.set_xlabel("x")
    ax.set_ylabel("Cp")
    ax.invert_yaxis()
    ax.grid(True, alpha=0.3)
    ax.set_title("Surface Pressure Coefficient")

    fig.tight_layout()

    out_path = os.path.join(output_dir, "cp_distribution.png")
    fig.savefig(out_path, dpi=150)
    plt.close(fig)
    print(f"Saved Cp plot to {out_path}")


def main():
    parser = argparse.ArgumentParser(description="Plot CFD solver results")
    parser.add_argument("--results-dir", required=True, help="Directory with CSV output files")
    parser.add_argument("--output-dir", required=True, help="Directory for plot images")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    plot_residuals(args.results_dir, args.output_dir)
    plot_forces(args.results_dir, args.output_dir)
    plot_cp(args.results_dir, args.output_dir)

    print("Done.")


if __name__ == "__main__":
    main()

"""Plot residual, force, and surface-Cp figures from CFD case output directories.

Usage:
    python plot_results.py --case-dir solver/results/naca0012_m015_inviscid \
                           --output-dir solver/report/figures/naca0012_m015_inviscid
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Optional

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import rcParams

# Publication-quality styling
rcParams.update({
    "font.family": "serif",
    "font.serif": ["DejaVu Serif", "Times New Roman", "Liberation Serif"],
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "legend.fontsize": 9,
    "figure.dpi": 150,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
    "axes.grid": True,
    "grid.alpha": 0.4,
    "grid.linestyle": "--",
    "lines.linewidth": 1.4,
    "axes.linewidth": 1.0,
})

_PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#ff7f0e",
            "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"]


def _read_csv(path):
    """Read a comma-delimited CSV with a header row into a structured array."""
    if not os.path.isfile(path):
        return None
    try:
        data = np.genfromtxt(path, delimiter=",", names=True, dtype=None,
                             encoding="utf-8")
        return data
    except Exception as exc:
        print(f"  [warn] failed to read {path}: {exc}", file=sys.stderr)
        return None


def _col(arr, name):
    """Return a named column from a structured array, or None."""
    if arr is None:
        return None
    if name in arr.dtype.names:
        return np.asarray(arr[name], dtype=float)
    return None


def _format_case_id(case_id):
    """Turn naca0012_m015_inviscid -> 'NACA0012, M=0.15, inviscid'."""
    cid = case_id.lower()
    if cid.startswith("naca"):
        body = "NACA0012"
        rest = cid[len("naca0012"):]
        mach_str = ""
        if "_m" in rest:
            after = rest.split("_m", 1)[1]
            mach_token = after.split("_", 1)[0]
            try:
                mach_str = f"M={int(mach_token)/100:.2f}"
            except ValueError:
                mach_str = f"M={mach_token}"
        if "_inviscid" in rest:
            visc = "inviscid"
        elif "_laminar" in rest:
            visc = "laminar"
            if "_re" in rest:
                re_part = rest.split("_re", 1)[1]
                re_token = re_part.split("_", 1)[0] if "_" in re_part else re_part
                try:
                    visc = f"laminar, Re={int(re_token)}"
                except ValueError:
                    visc = f"laminar, Re={re_token}"
        else:
            visc = ""
        parts = [p for p in [body, mach_str, visc] if p]
        return ", ".join(parts)
    if cid.startswith("cylinder"):
        rest = cid[len("cylinder"):]
        mach_str = ""
        if "_m" in rest:
            after = rest.split("_m", 1)[1]
            mach_token = after.split("_", 1)[0]
            try:
                mach_str = f"M={int(mach_token)/100:.2f}"
            except ValueError:
                mach_str = f"M={mach_token}"
        visc = ""
        if "_laminar" in rest:
            if "_re" in rest:
                re_part = rest.split("_re", 1)[1]
                re_token = re_part.split("_", 1)[0] if "_" in re_part else re_part
                try:
                    visc = f"laminar, Re={int(re_token)}"
                except ValueError:
                    visc = f"laminar, Re={re_token}"
        parts = [p for p in ["Cylinder", mach_str, visc] if p]
        return ", ".join(parts)
    return case_id


def _case_label(case_dir):
    meta_path = os.path.join(case_dir, "metadata.json")
    if os.path.isfile(meta_path):
        try:
            with open(meta_path) as fh:
                meta = json.load(fh)
            cid = meta.get("case_id")
            if cid:
                return _format_case_id(cid)
        except Exception:
            pass
    return os.path.basename(os.path.normpath(case_dir))


def _save(fig, out_dir, name):
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name)
    fig.savefig(path)
    plt.close(fig)
    print(f"  [fig] {path}")
    return path


def plot_residuals(case_dir, out_dir, label):
    paths = []
    res = _read_csv(os.path.join(case_dir, "residuals.csv"))
    if res is None:
        print("  [skip] residuals.csv not found", file=sys.stderr)
        return paths

    step = _col(res, "step")
    if step is None:
        print("  [skip] no 'step' column in residuals.csv", file=sys.stderr)
        return paths
    step = step.astype(float)

    res_vars = [
        ("rho",  r"$\rho$ residual",    "density"),
        ("rhou", r"$\rho u$ residual",   "x-momentum"),
        ("rhov", r"$\rho v$ residual",   "y-momentum"),
        ("rhoE", r"$\rho E$ residual",   "energy"),
    ]

    # Combined residual history
    fig, ax = plt.subplots(figsize=(8, 5))
    plotted = 0
    for i, (col, lbl, _desc) in enumerate(res_vars):
        y = _col(res, col)
        if y is None:
            continue
        y_abs = np.abs(y)
        y_abs = np.where(y_abs <= 0, 1e-300, y_abs)
        ax.semilogy(step, y_abs, color=_PALETTE[i % len(_PALETTE)],
                    label=lbl, alpha=0.9)
        plotted += 1
    if plotted == 0:
        plt.close(fig)
        return paths
    ax.set_xlabel("Iteration step")
    ax.set_ylabel("Absolute residual (log scale)")
    ax.set_title(f"Residual history - {label}")
    ax.legend(loc="best", framealpha=0.9)
    ax.grid(True, which="both", linestyle="--", alpha=0.4)
    fig.tight_layout()
    p = _save(fig, out_dir, "residuals_history.png")
    if p:
        paths.append(p)

    # L2 / Linf norms
    l2 = _col(res, "residual_l2")
    linf = _col(res, "residual_linf")
    if l2 is not None or linf is not None:
        fig, ax = plt.subplots(figsize=(8, 5))
        if l2 is not None:
            l2a = np.abs(l2)
            l2a = np.where(l2a <= 0, 1e-300, l2a)
            ax.semilogy(step, l2a, color="#1f77b4", label=r"$L_2$ norm", alpha=0.9)
        if linf is not None:
            la = np.abs(linf)
            la = np.where(la <= 0, 1e-300, la)
            ax.semilogy(step, la, color="#d62728", label=r"$L_\infty$ norm", alpha=0.9)
        ax.set_xlabel("Iteration step")
        ax.set_ylabel("Global residual norm (log scale)")
        ax.set_title(f"Global residual norms - {label}")
        ax.legend(loc="best", framealpha=0.9)
        ax.grid(True, which="both", linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "residuals_norms.png")
        if p:
            paths.append(p)

    # CFL / dt history
    cfl = _col(res, "cfl")
    dt = _col(res, "dt")
    if cfl is not None or dt is not None:
        fig, ax1 = plt.subplots(figsize=(8, 4.5))
        if cfl is not None:
            ax1.plot(step, cfl, color="#1f77b4", label="CFL", alpha=0.9)
            ax1.set_ylabel("CFL", color="#1f77b4")
        if dt is not None:
            ax2 = ax1.twinx()
            ax2.plot(step, dt, color="#d62728", label=r"$\Delta t$", alpha=0.7)
            ax2.set_ylabel(r"$\Delta t$", color="#d62728")
        ax1.set_xlabel("Iteration step")
        ax1.set_title(f"CFL / time-step history - {label}")
        ax1.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "cfl_history.png")
        if p:
            paths.append(p)

    return paths


def plot_forces(case_dir, out_dir, label):
    paths = []
    f = _read_csv(os.path.join(case_dir, "forces.csv"))
    if f is None:
        print("  [skip] forces.csv not found", file=sys.stderr)
        return paths

    step = _col(f, "step")
    if step is None:
        print("  [skip] no 'step' column in forces.csv", file=sys.stderr)
        return paths
    step = step.astype(float)

    cl = _col(f, "cl")
    cd = _col(f, "cd")
    if cl is not None or cd is not None:
        fig, ax = plt.subplots(figsize=(8, 5))
        if cl is not None:
            ax.plot(step, cl, color="#1f77b4", label=r"$C_L$", alpha=0.9)
        if cd is not None:
            ax.plot(step, cd, color="#d62728", label=r"$C_D$", alpha=0.9)
        ax.set_xlabel("Iteration step")
        ax.set_ylabel("Force coefficient")
        ax.set_title(f"Lift / drag coefficient history - {label}")
        ax.legend(loc="best", framealpha=0.9)
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "forces_cl_cd.png")
        if p:
            paths.append(p)

    cmz = _col(f, "cmz")
    if cmz is not None:
        fig, ax = plt.subplots(figsize=(8, 5))
        ax.plot(step, cmz, color="#2ca02c", label=r"$C_{m,z}$", alpha=0.9)
        ax.set_xlabel("Iteration step")
        ax.set_ylabel("Moment coefficient")
        ax.set_title(f"Pitching moment history - {label}")
        ax.legend(loc="best", framealpha=0.9)
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "forces_cmz.png")
        if p:
            paths.append(p)

    pd = _col(f, "pressure_drag")
    vd = _col(f, "viscous_drag")
    pl = _col(f, "pressure_lift")
    vl = _col(f, "viscous_lift")
    if pd is not None or vd is not None or pl is not None or vl is not None:
        fig, ax = plt.subplots(figsize=(8, 5))
        i = 0
        if pd is not None:
            ax.plot(step, pd, color=_PALETTE[i], label="Pressure drag", alpha=0.9); i += 1
        if vd is not None:
            ax.plot(step, vd, color=_PALETTE[i], label="Viscous drag", alpha=0.9); i += 1
        if pl is not None:
            ax.plot(step, pl, color=_PALETTE[i], label="Pressure lift", alpha=0.9); i += 1
        if vl is not None:
            ax.plot(step, vl, color=_PALETTE[i], label="Viscous lift", alpha=0.9); i += 1
        ax.set_xlabel("Iteration step")
        ax.set_ylabel("Force component")
        ax.set_title(f"Pressure / viscous force decomposition - {label}")
        ax.legend(loc="best", framealpha=0.9)
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "forces_decomposition.png")
        if p:
            paths.append(p)

    if cl is not None and cd is not None and len(step) > 20:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
        tail = max(1, len(step) // 5)
        ax1.plot(step[-tail:], cl[-tail:], color="#1f77b4", alpha=0.9)
        ax1.set_xlabel("Iteration step")
        ax1.set_ylabel(r"$C_L$")
        ax1.set_title(f"$C_L$ (last {tail} steps)")
        ax1.grid(True, linestyle="--", alpha=0.4)
        ax2.plot(step[-tail:], cd[-tail:], color="#d62728", alpha=0.9)
        ax2.set_xlabel("Iteration step")
        ax2.set_ylabel(r"$C_D$")
        ax2.set_title(f"$C_D$ (last {tail} steps)")
        ax2.grid(True, linestyle="--", alpha=0.4)
        fig.suptitle(f"Force convergence tail - {label}", fontsize=12)
        fig.tight_layout(rect=[0, 0, 1, 0.95])
        p = _save(fig, out_dir, "forces_convergence_tail.png")
        if p:
            paths.append(p)

    return paths


def plot_surface_cp(case_dir, out_dir, label, case_type="naca"):
    paths = []
    surf = _read_csv(os.path.join(case_dir, "surface.csv"))
    if surf is None:
        print("  [skip] surface.csv not found", file=sys.stderr)
        return paths

    x = _col(surf, "x")
    y = _col(surf, "y")
    cp = _col(surf, "cp")
    cf = _col(surf, "cf")
    if x is None or y is None or cp is None:
        print("  [skip] missing x/y/cp in surface.csv", file=sys.stderr)
        return paths

    if case_type == "naca":
        upper = y >= 0
        lower = y < 0
        xu, yu, cpu = x[upper], y[upper], cp[upper]
        xl, yl, cpl = x[lower], y[lower], cp[lower]
        ou = np.argsort(xu)
        ol = np.argsort(xl)
        xu, yu, cpu = xu[ou], yu[ou], cpu[ou]
        xl, yl, cpl = xl[ol], yl[ol], cpl[ol]

        fig, ax = plt.subplots(figsize=(9, 5.5))
        ax.plot(xu, cpu, "o-", color="#1f77b4", markersize=2,
                label="Upper surface", alpha=0.85)
        ax.plot(xl, cpl, "s-", color="#d62728", markersize=2,
                label="Lower surface", alpha=0.85)
        ax.invert_yaxis()
        ax.set_xlabel(r"$x/c$")
        ax.set_ylabel(r"$C_p$")
        ax.set_title(f"Surface pressure coefficient - {label}")
        ax.legend(loc="best", framealpha=0.9)
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "surface_cp_naca.png")
        if p:
            paths.append(p)

        if cf is not None:
            cfu = cf[upper][ou]
            cfl_ = cf[lower][ol]
            fig, ax = plt.subplots(figsize=(9, 5.5))
            ax.plot(xu, cfu, "o-", color="#1f77b4", markersize=2,
                    label="Upper surface", alpha=0.85)
            ax.plot(xl, cfl_, "s-", color="#d62728", markersize=2,
                    label="Lower surface", alpha=0.85)
            ax.set_xlabel(r"$x/c$")
            ax.set_ylabel(r"$C_f$")
            ax.set_title(f"Skin friction coefficient - {label}")
            ax.legend(loc="best", framealpha=0.9)
            ax.grid(True, linestyle="--", alpha=0.4)
            fig.tight_layout()
            p = _save(fig, out_dir, "surface_cf_naca.png")
            if p:
                paths.append(p)

    elif case_type == "cylinder":
        theta = np.arctan2(y, x)
        theta = np.where(theta < 0, theta + 2 * np.pi, theta)
        o = np.argsort(theta)
        theta_s, cp_s = theta[o], cp[o]

        fig, ax = plt.subplots(figsize=(9, 5.5))
        ax.plot(np.degrees(theta_s), cp_s, "-", color="#1f77b4", alpha=0.85)
        ax.invert_yaxis()
        ax.set_xlabel(r"$\theta$ (degrees)")
        ax.set_ylabel(r"$C_p$")
        ax.set_title(f"Surface pressure coefficient - {label}")
        ax.set_xlim(0, 360)
        ax.grid(True, linestyle="--", alpha=0.4)
        fig.tight_layout()
        p = _save(fig, out_dir, "surface_cp_cylinder.png")
        if p:
            paths.append(p)

        if cf is not None:
            cf_s = cf[o]
            fig, ax = plt.subplots(figsize=(9, 5.5))
            ax.plot(np.degrees(theta_s), cf_s, "-", color="#d62728", alpha=0.85)
            ax.set_xlabel(r"$\theta$ (degrees)")
            ax.set_ylabel(r"$C_f$")
            ax.set_title(f"Skin friction coefficient - {label}")
            ax.set_xlim(0, 360)
            ax.grid(True, linestyle="--", alpha=0.4)
            fig.tight_layout()
            p = _save(fig, out_dir, "surface_cf_cylinder.png")
            if p:
                paths.append(p)

    return paths


def detect_case_type(case_dir, label):
    bn = os.path.basename(case_dir).lower()
    if "cylinder" in label.lower() or "cylinder" in bn:
        return "cylinder"
    return "naca"


def main():
    ap = argparse.ArgumentParser(description="Plot CFD residuals, forces, and surface Cp")
    ap.add_argument("--case-dir", required=True, help="Case output directory")
    ap.add_argument("--output-dir", required=True, help="Directory for PNG figures")
    ap.add_argument("--case-type", choices=["naca", "cylinder", "auto"], default="auto")
    args = ap.parse_args()

    case_dir = os.path.abspath(args.case_dir)
    out_dir = os.path.abspath(args.output_dir)
    label = _case_label(case_dir)

    if args.case_type == "auto":
        case_type = detect_case_type(case_dir, label)
    else:
        case_type = args.case_type

    print(f"Plotting results for case: {label}  (type={case_type})")
    print(f"  case_dir:   {case_dir}")
    print(f"  output_dir: {out_dir}")

    all_paths = []
    all_paths += plot_residuals(case_dir, out_dir, label)
    all_paths += plot_forces(case_dir, out_dir, label)
    all_paths += plot_surface_cp(case_dir, out_dir, label, case_type)

    print(f"\n  Generated {len(all_paths)} figures.")
    return 0 if all_paths else 1


if __name__ == "__main__":
    sys.exit(main())

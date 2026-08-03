#!/usr/bin/env python3
"""Plot convergence history (residuals) for one solver case.

Usage:
    python plot_residuals.py <case_dir> [case_id] [figures_dir]

Output: <figures_dir>/<case_id>_residual.png
Two panels: global L2 residual (log) and per-component residuals (log).
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import (ensure_figures_dir, figure_path, read_csv,
                        setup_figure)  # noqa: E402

COMPONENTS = [("rho", r"$\rho$"), ("rhou", r"$\rho u$"),
              ("rhov", r"$\rho v$"), ("rhoE", r"$\rho E$")]

MAX_POINTS = 20000  # down-sample very long histories for plotting


def _subsample(x, *ys):
    """Return x, ys with at most MAX_POINTS points (uniform stride)."""
    n = len(x)
    if n <= MAX_POINTS:
        return x, *ys
    stride = int(np.ceil(n / MAX_POINTS))
    idx = np.arange(0, n, stride)
    return x[idx], *(y[idx] for y in ys)


def plot_residuals(case_dir, case_id, figures_dir):
    csv_path = os.path.join(case_dir, "residuals.csv")
    data = read_csv(csv_path)
    if data is None:
        print(f"  SKIP {case_id}: no residuals.csv")
        return False

    step = data["step"]
    rl2 = data["residual_l2"]
    rlinf = data["residual_linf"]
    step, rl2, rlinf = _subsample(step, rl2, rlinf)
    l2_ok = np.isfinite(rl2) & (rl2 > 0.0)
    linf_ok = np.isfinite(rlinf) & (rlinf > 0.0)
    if l2_ok.sum() < 2:
        print(f"  SKIP {case_id}: no usable residual values")
        return False

    fig, axes = plt.subplots(
        2, 1, figsize=(6.5, 5.4), sharex=True,
        gridspec_kw={"height_ratios": [1.0, 1.35]})

    # Panel 1: global residual norms.
    ax = axes[0]
    ax.semilogy(step[l2_ok], rl2[l2_ok], color="C0", lw=1.2, label=r"$L_2$")
    ax.semilogy(step[linf_ok], rlinf[linf_ok], color="C1", lw=1.0,
                ls="--", label=r"$L_\infty$")
    ax.set_ylabel(r"Residual")
    ax.legend(loc="upper right", ncol=1)
    ax.set_title(f"{case_id} — convergence history", pad=8)

    # Panel 2: per-component L2 residuals.
    ax = axes[1]
    comp_ys = []
    for name, label in COMPONENTS:
        y = data[name]
        y = np.where(np.isfinite(y) & (y > 0.0), y, np.nan)
        comp_ys.append(y)
    step2, *comp_ys = _subsample(step, *comp_ys)
    for (name, label), y in zip(COMPONENTS, comp_ys):
        ax.semilogy(step2, y, lw=0.9, label=label)
    ax.set_xlabel("Iteration step")
    ax.set_ylabel(r"Component $L_2$ residual")
    ax.legend(loc="upper right", ncol=4, fontsize=8)

    fig.align_ylabels(axes)
    out = figure_path(figures_dir, case_id, "residual")
    fig.savefig(out)
    plt.close(fig)
    print(f"  wrote {os.path.relpath(out)}")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case_dir", help="case result directory")
    parser.add_argument("case_id", nargs="?", default=None,
                        help="case id (default: basename of case_dir)")
    parser.add_argument("figures_dir", nargs="?",
                        default=os.path.join(os.path.dirname(__file__),
                                             "..", "report", "figures"))
    args = parser.parse_args()
    case_id = args.case_id or os.path.basename(os.path.normpath(args.case_dir))

    setup_figure()
    ensure_figures_dir(args.figures_dir)
    ok = plot_residuals(args.case_dir, case_id, args.figures_dir)
    # exit 2 signals "no figure produced" (missing data or unusable input);
    # the master script treats it as an informational skip, not a failure.
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()

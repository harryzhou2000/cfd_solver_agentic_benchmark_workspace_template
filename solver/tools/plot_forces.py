#!/usr/bin/env python3
"""Plot aerodynamic force coefficient histories for one solver case.

Usage:
    python plot_forces.py <case_dir> [case_id] [figures_dir]

Output: <figures_dir>/<case_id>_forces.png
Two panels: lift coefficient C_l and drag coefficient C_d versus iteration
step, or versus physical time for transient cases (Re200 cylinder).
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import (ensure_figures_dir, figure_path, read_csv,
                        setup_figure)  # noqa: E402

MAX_POINTS = 20000  # down-sample very long histories for plotting


def _subsample(x, *ys):
    n = len(x)
    if n <= MAX_POINTS:
        return x, *ys
    stride = int(np.ceil(n / MAX_POINTS))
    idx = np.arange(0, n, stride)
    return x[idx], *(y[idx] for y in ys)


def plot_forces(case_dir, case_id, figures_dir):
    csv_path = os.path.join(case_dir, "forces.csv")
    data = read_csv(csv_path)
    if data is None:
        print(f"  SKIP {case_id}: no forces.csv")
        return False

    step = data["step"]
    t = data["physical_time"]
    transient = bool(np.nanmax(t) > 0.0)

    x = t if transient else step
    xlabel = (r"Physical time $t$" if transient else
              "Iteration step")
    x, cl, cd = _subsample(x, data["cl"], data["cd"])

    fig, axes = plt.subplots(2, 1, figsize=(6.5, 4.8), sharex=True)

    ax = axes[0]
    ax.plot(x, cl, color="C0", lw=1.1, label=r"$C_l$")
    ax.axhline(0.0, color="0.5", lw=0.6, ls=":")
    ax.set_ylabel(r"Lift coefficient $C_l$")
    ax.set_title(f"{case_id} — force coefficients", pad=8)
    ax.legend(loc="best")

    ax = axes[1]
    ax.plot(x, cd, color="C3", lw=1.1, label=r"$C_d$")
    ax.set_ylabel(r"Drag coefficient $C_d$")
    ax.set_xlabel(xlabel)
    ax.legend(loc="best")

    fig.align_ylabels(axes)
    out = figure_path(figures_dir, case_id, "forces")
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
    ok = plot_forces(args.case_dir, case_id, args.figures_dir)
    # exit 2 signals "no figure produced" (missing data or unusable input);
    # the master script treats it as an informational skip, not a failure.
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()

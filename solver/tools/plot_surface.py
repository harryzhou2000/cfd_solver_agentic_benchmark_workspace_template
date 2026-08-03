#!/usr/bin/env python3
"""Plot surface pressure coefficient distributions for one solver case.

Usage:
    python plot_surface.py <case_dir> [case_id] [figures_dir]

Output: <figures_dir>/<case_id>_cp.png

- NACA airfoils: C_p vs x/c, sorted by x (C_p axis inverted, so the suction
  side appears above — aerodynamic convention).
- Cylinders: C_p vs theta (angle around the cylinder centre, degrees).
"""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import (case_kind, ensure_figures_dir, figure_path,
                        read_csv, setup_figure)  # noqa: E402


def plot_surface(case_dir, case_id, figures_dir):
    csv_path = os.path.join(case_dir, "surface.csv")
    data = read_csv(csv_path)
    if data is None:
        print(f"  SKIP {case_id}: no surface.csv")
        return False

    x = data["x"]
    y = data["y"]
    cp = data["cp"]

    fig, ax = plt.subplots(figsize=(6.5, 4.4))

    kind = case_kind(case_id)
    if kind == "cylinder":
        # Angle around the cylinder centre (closed body: mean of the wall
        # points is a robust centre estimate).
        xc, yc = float(np.mean(x)), float(np.mean(y))
        theta = np.degrees(np.arctan2(y - yc, x - xc))
        order = np.argsort(theta)
        ax.plot(theta[order], cp[order], color="C0", lw=1.3,
                label=r"$C_p(\theta)$")
        ax.set_xlabel(r"Angle $\theta$ (deg)")
        ax.set_xlim(-180.0, 180.0)
    elif kind == "naca":
        order = np.argsort(x)
        ax.plot(x[order], cp[order], color="C0", lw=1.3,
                label=r"$C_p(x/c)$")
        ax.set_xlabel(r"Chord position $x/c$")
        ax.invert_yaxis()  # suction side up (aerodynamic convention)
        ax.axhline(0.0, color="0.5", lw=0.6, ls=":")
    else:
        order = np.argsort(x)
        ax.plot(x[order], cp[order], color="C0", lw=1.3)
        ax.set_xlabel(r"$x$")

    ax.set_ylabel(r"Pressure coefficient $C_p$")
    ax.set_title(f"{case_id} — surface pressure", pad=8)
    ax.legend(loc="best")

    out = figure_path(figures_dir, case_id, "cp")
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
    ok = plot_surface(args.case_dir, case_id, args.figures_dir)
    # exit 2 signals "no figure produced" (missing data or unusable input);
    # the master script treats it as an informational skip, not a failure.
    sys.exit(0 if ok else 2)


if __name__ == "__main__":
    main()

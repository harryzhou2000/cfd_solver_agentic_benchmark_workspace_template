#!/usr/bin/env python3
"""Publication-style residual and force history plots for one case."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from cfd_io import read_csv


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("case_dir", type=Path)
    ap.add_argument("--figdir", type=Path, default=None)
    args = ap.parse_args()
    case_dir = args.case_dir.resolve()
    figdir = (args.figdir or case_dir / ".." / ".." / "report" / "figures").resolve()
    figdir.mkdir(parents=True, exist_ok=True)
    case_id = case_dir.name

    res = read_csv(case_dir / "residuals.csv")
    force = read_csv(case_dir / "forces.csv")

    plt.rcParams.update({
        "font.size": 10,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "lines.linewidth": 1.2,
        "figure.dpi": 150,
    })

    steps = [int(r["step"]) for r in res]
    rl2 = [float(r["residual_l2"]) for r in res]
    comp = ["rho", "rhou", "rhov", "rhoE"]
    fig, ax = plt.subplots(figsize=(7.5, 3.6))
    ax.semilogy(steps, rl2, label="total L2", color="black")
    for key, c in zip(comp, ["C0", "C1", "C2", "C3"]):
        ax.semilogy(steps, [float(r[key]) for r in res], color=c, alpha=0.7,
                    label=key)
    xlabel = ("physical step"
              if force and float(force[-1]["physical_time"]) > 0
              else "pseudo step")
    ax.set_xlabel(xlabel)
    ax.set_ylabel("residual norm")
    ax.set_title(f"{case_id}: residual history")
    ax.legend(ncol=5, fontsize=8)
    fig.tight_layout()
    fig.savefig(figdir / f"{case_id}_residual.png")
    plt.close(fig)

    t = [float(r["physical_time"]) for r in force]
    cd = [float(r["cd"]) for r in force]
    cl = [float(r["cl"]) for r in force]
    fig, ax = plt.subplots(figsize=(7.5, 3.6))
    ax.plot(t, cd, label="$C_D$")
    ax.plot(t, cl, label="$C_L$")
    ax.set_xlabel("physical time" if t and t[-1] > 0 else "pseudo step")
    ax.set_ylabel("force coefficient")
    ax.set_title(f"{case_id}: force history")
    ax.legend()
    fig.tight_layout()
    fig.savefig(figdir / f"{case_id}_forces.png")
    plt.close(fig)
    print(f"wrote residual/force plots to {figdir}")


if __name__ == "__main__":
    main()

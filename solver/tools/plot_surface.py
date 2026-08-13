#!/usr/bin/env python3
"""Wall surface distributions: Cp (and Cf when viscous)."""

from __future__ import annotations

import argparse
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
    rows = read_csv(case_dir / "surface.csv")
    x = [float(r["x"]) for r in rows]
    y = [float(r["y"]) for r in rows]
    cp = [float(r["cp"]) for r in rows]
    cf = [float(r["cf"]) for r in rows]

    plt.rcParams.update({
        "font.size": 10,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "lines.linewidth": 1.2,
        "figure.dpi": 150,
    })

    fig, ax = plt.subplots(figsize=(7.5, 3.8))
    ax.scatter(x, cp, s=18, c=y, cmap="coolwarm", label="$C_p$")
    ax.set_xlabel("x")
    ax.set_ylabel("$C_p$")
    ax.set_title(f"{case_id}: wall pressure coefficient")
    fig.colorbar(ax.collections[0], ax=ax, label="y (surface side)")
    fig.tight_layout()
    fig.savefig(figdir / f"{case_id}_cp.png")
    plt.close(fig)

    if any(abs(v) > 1e-12 for v in cf):
        fig, ax = plt.subplots(figsize=(7.5, 3.8))
        ax.scatter(x, cf, s=18, c=y, cmap="coolwarm", label="$c_f$")
        ax.set_xlabel("x")
        ax.set_ylabel("$c_f$")
        ax.set_title(f"{case_id}: wall skin-friction coefficient")
        fig.colorbar(ax.collections[0], ax=ax, label="y (surface side)")
        fig.tight_layout()
        fig.savefig(figdir / f"{case_id}_cf.png")
        plt.close(fig)
    print(f"wrote surface plots to {figdir}")


if __name__ == "__main__":
    main()

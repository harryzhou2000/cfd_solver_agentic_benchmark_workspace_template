#!/usr/bin/env python3
"""Filled field visualizations (Mach, pressure, vorticity/velocity) from the
solver's VTU output, rendered on the actual unstructured cells."""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np

from cfd_io import parse_vtu, read_json, split_cells_to_tris, clip_percentiles


def render_field(case_id: str, data: dict, field: str, out: Path,
                 clip: tuple[float, float] | None = None,
                 xlim: tuple[float, float] | None = None,
                 ylim: tuple[float, float] | None = None,
                 title: str | None = None) -> None:
    xs, ys, vals = split_cells_to_tris(data, field)
    tri = mtri.Triangulation(xs, ys)
    vmin, vmax = clip if clip else clip_percentiles(vals, 0.5, 99.5)
    fig, ax = plt.subplots(figsize=(9, 5))
    tcf = ax.tripcolor(tri, vals, shading="gouraud", cmap="viridis",
                       vmin=vmin, vmax=vmax)
    cb = fig.colorbar(tcf, ax=ax)
    cb.set_label(field)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    if xlim:
        ax.set_xlim(*xlim)
    if ylim:
        ax.set_ylim(*ylim)
    ax.set_title(title or f"{case_id}: {field} field")
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("case_dir", type=Path)
    ap.add_argument("--field", default="field_final.vtu")
    ap.add_argument("--figdir", type=Path, default=None)
    ap.add_argument("--zoom", type=float, default=None)
    args = ap.parse_args()
    case_dir = args.case_dir.resolve()
    field_path = case_dir / args.field
    figdir = (args.figdir or case_dir / ".." / ".." / "report" / "figures").resolve()
    figdir.mkdir(parents=True, exist_ok=True)
    case_id = read_json(case_dir / "metadata.json").get("case_id",
                                                        case_dir.name)
    data = parse_vtu(field_path)

    window = (-args.zoom, args.zoom) if args.zoom else None
    mach_vmax = max(1.6, clip_percentiles(data["mach"], 0.5, 99.5)[1])
    render_field(case_id, data, "mach", figdir / f"{case_id}_mach.png",
                 clip=(0.0, mach_vmax), xlim=window, ylim=window,
                 title=f"{case_id}: Mach number")
    render_field(case_id, data, "p", figdir / f"{case_id}_pressure.png",
                 xlim=window, ylim=window,
                 title=f"{case_id}: pressure")
    if "vort" in data:
        render_field(case_id, data, "vort", figdir / f"{case_id}_vorticity.png",
                     clip=(-5.0, 5.0), xlim=window, ylim=window,
                     title=f"{case_id}: vorticity $\\omega_z$")
    print(f"wrote field plots to {figdir}")


if __name__ == "__main__":
    main()

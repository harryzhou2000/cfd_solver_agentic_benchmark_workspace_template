#!/usr/bin/env python3
"""Generate traceable publication-style figures from SaturnCFD outputs."""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import xml.etree.ElementTree as ET

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
import numpy as np


CASE_ORDER = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def style() -> None:
    plt.rcParams.update({
        "font.size": 9,
        "axes.labelsize": 10,
        "axes.titlesize": 10,
        "legend.fontsize": 8,
        "lines.linewidth": 1.25,
        "axes.grid": True,
        "grid.alpha": 0.25,
        "figure.dpi": 130,
        "savefig.dpi": 220,
        "savefig.bbox": "tight",
    })


def read_parallel_vtu(master: Path) -> tuple[list[np.ndarray], dict[str, np.ndarray]]:
    root = ET.parse(master).getroot()
    piece_nodes = root.findall(".//Piece")
    if not piece_nodes:
        raise ValueError(f"no pieces in {master}")
    polygons: list[np.ndarray] = []
    fields: dict[str, list[np.ndarray]] = {}
    for piece_node in piece_nodes:
        piece = master.parent / piece_node.attrib["Source"]
        piece_root = ET.parse(piece).getroot()
        points_node = piece_root.find(".//Points/DataArray")
        if points_node is None or points_node.text is None:
            raise ValueError(f"missing points in {piece}")
        points = np.fromstring(points_node.text, sep=" ").reshape(-1, 3)[:, :2]
        arrays = {node.attrib.get("Name"): node for node in piece_root.findall(".//Cells/DataArray")}
        connectivity = np.fromstring(arrays["connectivity"].text or "", sep=" ", dtype=np.int64)
        offsets = np.fromstring(arrays["offsets"].text or "", sep=" ", dtype=np.int64)
        start = 0
        for end in offsets:
            polygons.append(points[connectivity[start:end]])
            start = int(end)
        for node in piece_root.findall(".//CellData/DataArray"):
            name = node.attrib["Name"]
            fields.setdefault(name, []).append(np.fromstring(node.text or "", sep=" "))
    merged = {name: np.concatenate(parts) for name, parts in fields.items()}
    if any(len(values) != len(polygons) for values in merged.values()):
        raise ValueError(f"cell-data length mismatch in {master}")
    return polygons, merged


def downsample(x: np.ndarray, *ys: np.ndarray, maximum: int = 12_000) -> tuple[np.ndarray, ...]:
    if len(x) <= maximum:
        return (x, *ys)
    indices = np.linspace(0, len(x) - 1, maximum, dtype=np.int64)
    return (x[indices], *(y[indices] for y in ys))


def result_path(report: Path, source: Path) -> str:
    return os.path.relpath(source, report)


def save_line_figures(case_id: str, result: Path, figures: Path, report: Path,
                      manifest: list[dict[str, str]]) -> None:
    residual_data = np.genfromtxt(result / "residuals.csv", delimiter=",", names=True,
                                  usecols=(0, 1, 9), dtype=float)
    residual_data = np.atleast_1d(residual_data)
    step = residual_data["step"]
    physical_time = residual_data["physical_time"]
    residual = residual_data["residual_l2"]
    if len(step) > 1 and np.any(physical_time > 0.0):
        last_of_step = np.r_[step[1:] != step[:-1], True]
        x = physical_time[last_of_step]
        y = residual[last_of_step]
        xlabel = r"physical time $tU_\infty/L$"
    else:
        x, y = step, residual
        xlabel = "pseudo-time step"
    x, y = downsample(x, y)
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    ax.semilogy(x, np.maximum(y, np.finfo(float).tiny), color="#204a87")
    ax.set(xlabel=xlabel, ylabel=r"global total residual $L_2$", title=case_id.replace("_", " "))
    fig.tight_layout()
    filename = f"{case_id}_residual.png"
    fig.savefig(figures / filename)
    plt.close(fig)
    manifest.append({"figure_file": filename, "case_id": case_id, "figure_type": "history",
                     "variable": "residual", "source_file": result_path(report, result / "residuals.csv"),
                     "caption": f"Global residual history for {case_id}."})

    force_data = np.genfromtxt(result / "forces.csv", delimiter=",", names=True, dtype=float)
    force_data = np.atleast_1d(force_data)
    x = force_data["physical_time"] if np.any(force_data["physical_time"] > 0.0) else force_data["step"]
    xlabel = r"physical time $tU_\infty/L$" if np.any(force_data["physical_time"] > 0.0) else "pseudo-time step"
    x, cd, cl = downsample(x, force_data["cd"], force_data["cl"])
    fig, ax = plt.subplots(figsize=(5.6, 3.4))
    ax.plot(x, cd, label=r"$C_D$", color="#a40000")
    ax.plot(x, cl, label=r"$C_L$", color="#204a87")
    ax.set(xlabel=xlabel, ylabel="force coefficient", title=case_id.replace("_", " "))
    ax.legend(ncol=2)
    fig.tight_layout()
    filename = f"{case_id}_forces.png"
    fig.savefig(figures / filename)
    plt.close(fig)
    manifest.append({"figure_file": filename, "case_id": case_id, "figure_type": "history",
                     "variable": "lift and drag coefficients", "source_file": result_path(report, result / "forces.csv"),
                     "caption": f"Lift and drag coefficient history for {case_id}."})


def save_surface_figure(case_id: str, result: Path, figures: Path, report: Path,
                        manifest: list[dict[str, str]]) -> None:
    surface = np.genfromtxt(result / "surface.csv", delimiter=",", names=True,
                            usecols=range(11), dtype=float)
    surface = np.atleast_1d(surface)
    viscous = "laminar" in case_id
    if case_id.startswith("naca"):
        key = surface["x"]
        order = np.argsort(key)
        xlabel = r"$x/c$"
    else:
        key = np.degrees(np.arctan2(surface["y"], surface["x"]))
        order = np.argsort(key)
        xlabel = r"cylinder angle $\theta$ (deg)"
    rows = 2 if viscous else 1
    fig, axes = plt.subplots(rows, 1, figsize=(5.6, 2.8 + 1.8 * (rows - 1)), sharex=True,
                             squeeze=False)
    axes[0, 0].plot(key[order], surface["cp"][order], color="#204a87")
    axes[0, 0].set_ylabel(r"$C_p$")
    if case_id.startswith("naca"):
        axes[0, 0].invert_yaxis()
    if viscous:
        axes[1, 0].plot(key[order], surface["cf"][order], color="#a40000")
        axes[1, 0].axhline(0.0, color="black", linewidth=0.6)
        axes[1, 0].set_ylabel(r"$C_f$")
    axes[-1, 0].set_xlabel(xlabel)
    axes[0, 0].set_title(case_id.replace("_", " ") + " wall distribution")
    fig.tight_layout()
    filename = f"{case_id}_surface.png"
    fig.savefig(figures / filename)
    plt.close(fig)
    variable = "pressure coefficient and skin friction" if viscous else "pressure coefficient"
    manifest.append({"figure_file": filename, "case_id": case_id, "figure_type": "surface_distribution",
                     "variable": variable, "source_file": result_path(report, result / "surface.csv"),
                     "caption": f"Wall {variable} for {case_id}."})


def view_for(case_id: str) -> tuple[tuple[float, float], tuple[float, float]]:
    if case_id.startswith("naca"):
        return (-0.15, 1.15), (-0.32, 0.32)
    return (-1.5, 8.0), (-3.0, 3.0)


def save_field_figure(case_id: str, variable: str, label: str, polygons: list[np.ndarray],
                      values: np.ndarray, result: Path, figures: Path, report: Path,
                      manifest: list[dict[str, str]], clip: tuple[float, float] | None = None) -> None:
    xlim, ylim = view_for(case_id)
    centers = np.array([polygon.mean(axis=0) for polygon in polygons])
    mask = ((centers[:, 0] >= xlim[0] - 1.0) & (centers[:, 0] <= xlim[1] + 1.0) &
            (centers[:, 1] >= ylim[0] - 1.0) & (centers[:, 1] <= ylim[1] + 1.0))
    selected_polygons = [polygon for polygon, keep in zip(polygons, mask) if keep]
    selected_values = values[mask]
    if clip is None:
        low, high = np.nanpercentile(selected_values, (0.5, 99.5))
        if not np.isfinite(low + high) or high <= low:
            low, high = float(np.nanmin(selected_values)), float(np.nanmax(selected_values) + 1.0e-12)
    else:
        low, high = clip
    fig, ax = plt.subplots(figsize=(7.2, 3.5 if case_id.startswith("naca") else 4.2))
    collection = PolyCollection(selected_polygons, array=selected_values, cmap="coolwarm" if variable == "vorticity" else "viridis",
                                edgecolors="none", linewidths=0.0)
    collection.set_clim(low, high)
    ax.add_collection(collection)
    ax.set(xlim=xlim, ylim=ylim, xlabel=r"$x/L$", ylabel=r"$y/L$",
           title=f"{case_id.replace('_', ' ')}: {label}")
    ax.set_aspect("equal", adjustable="box")
    ax.grid(False)
    colorbar = fig.colorbar(collection, ax=ax, pad=0.02)
    colorbar.set_label(label + (f" (clipped to [{low:g}, {high:g}])" if clip else ""))
    fig.tight_layout()
    filename = f"{case_id}_{variable}.png"
    fig.savefig(figures / filename)
    plt.close(fig)
    manifest.append({"figure_file": filename, "case_id": case_id, "figure_type": "unstructured_cell_rendering",
                     "variable": variable, "source_file": result_path(report, result / "field_final.pvtu"),
                     "caption": f"Computed {label} field for {case_id} on the actual unstructured cells."})


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=root / "solver/results")
    parser.add_argument("--report", type=Path, default=root / "solver/report")
    args = parser.parse_args()
    figures = args.report / "figures"
    figures.mkdir(parents=True, exist_ok=True)
    style()
    manifest: list[dict[str, str]] = []
    for case_id in CASE_ORDER:
        result = args.results / case_id
        if not (result / "field_final.pvtu").is_file():
            raise FileNotFoundError(f"missing completed field for {case_id}: {result}")
        save_line_figures(case_id, result, figures, args.report, manifest)
        save_surface_figure(case_id, result, figures, args.report, manifest)
        polygons, fields = read_parallel_vtu(result / "field_final.pvtu")
        save_field_figure(case_id, "mach", r"Mach number $M$", polygons, fields["Mach"],
                          result, figures, args.report, manifest)
        save_field_figure(case_id, "pressure", r"nondimensional pressure $p$", polygons, fields["Pressure"],
                          result, figures, args.report, manifest)
        if case_id == "cylinder_m010_laminar_re200":
            save_field_figure(case_id, "vorticity", r"vorticity $\omega_z L/U_\infty$", polygons,
                              fields["Vorticity"], result, figures, args.report, manifest, (-5.0, 5.0))
    manifest_path = args.report / "figure_manifest.csv"
    with manifest_path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=("figure_file", "case_id", "figure_type", "variable", "source_file", "caption"))
        writer.writeheader()
        writer.writerows(manifest)
    print(f"wrote {len(manifest)} figures to {figures}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

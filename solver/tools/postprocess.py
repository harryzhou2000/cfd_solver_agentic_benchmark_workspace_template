#!/usr/bin/env python3
"""Generate contract-traceable publication-style PNGs from CFD outputs."""
from __future__ import annotations

import argparse
import csv
import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
BENCHMARK = ROOT.parent / "cfd_solver_agentic_benchmark"
CASE_DIR = BENCHMARK / "inputs" / "cases"
EXPECTED_CASE_IDS = {path.stem for path in CASE_DIR.glob("*.json")}
FIGURES = ROOT / "report" / "figures"
MANIFEST_COLUMNS = ["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"]


def csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def numeric(rows: list[dict[str, str]], key: str) -> np.ndarray:
    return np.asarray([float(r[key]) for r in rows], dtype=float)


def save_line(path: Path, x: np.ndarray, ys: list[tuple[str, np.ndarray]], xlabel: str, ylabel: str, title: str, logy: bool = False) -> None:
    fig, ax = plt.subplots(figsize=(7.0, 4.3), constrained_layout=True)
    for label, y in ys:
        ax.plot(x, y, linewidth=1.8, label=label)
    if logy:
        ax.set_yscale("log")
    ax.set(xlabel=xlabel, ylabel=ylabel, title=title)
    ax.grid(True, alpha=.3)
    ax.legend(frameon=False)
    fig.savefig(path, dpi=190)
    plt.close(fig)


def parse_legacy_vtk(path: Path) -> tuple[np.ndarray, np.ndarray, dict[str, np.ndarray]]:
    """Small reader for ASCII legacy VTK unstructured grids used by this project."""
    tokens = path.read_text(errors="replace").split()
    points_i = tokens.index("POINTS")
    npoints = int(tokens[points_i + 1]); start = points_i + 3
    points = np.asarray([float(x) for x in tokens[start:start + 3*npoints]], dtype=float).reshape(npoints, 3)
    cells: list[list[int]] = []
    if "CELLS" in tokens:
        ci = tokens.index("CELLS"); count = int(tokens[ci+1]); pos = ci+3
        for _ in range(count):
            count_i = int(tokens[pos]); cells.append([int(x) for x in tokens[pos+1:pos+1+count_i]]); pos += count_i+1
    # Split each quad into two triangles and retain a source-cell map so every
    # cell-centred scalar is duplicated onto both triangles.  Keeping only the
    # first three vertices silently erased half of every quad in contour plots.
    triangles_list: list[list[int]] = []
    triangle_source: list[int] = []
    for source, cell in enumerate(cells):
        if len(cell) == 3:
            triangles_list.append(cell); triangle_source.append(source)
        elif len(cell) == 4:
            triangles_list.extend([[cell[0], cell[1], cell[2]], [cell[0], cell[2], cell[3]]])
            triangle_source.extend([source, source])
    triangles = np.asarray(triangles_list, dtype=int)
    data: dict[str, np.ndarray] = {}
    # Supports scalar CELL_DATA/POINT_DATA fields, with values following LOOKUP_TABLE.
    active_size = npoints
    for i, token in enumerate(tokens):
        if token == "POINT_DATA" and i + 1 < len(tokens):
            active_size = int(tokens[i + 1])
        elif token == "CELL_DATA" and i + 1 < len(tokens):
            active_size = int(tokens[i + 1])
        if token == "SCALARS" and i + 2 < len(tokens):
            name = tokens[i+1]; p = i + 3
            if p < len(tokens) and tokens[p].isdigit(): p += 1
            while p < len(tokens) and tokens[p] != "LOOKUP_TABLE": p += 1
            if p + 2 >= len(tokens): continue
            values = tokens[p+2:p+2+active_size]
            try:
                parsed = np.asarray([float(x) for x in values])
                if active_size == len(cells) and len(parsed) == len(cells):
                    parsed = parsed[np.asarray(triangle_source, dtype=int)]
                data[name.lower()] = parsed

            except ValueError: pass
    return points[:, :2], triangles, data


def parse_vtu(path: Path) -> tuple[np.ndarray, np.ndarray, dict[str, np.ndarray]]:
    """Read ASCII XML VTU point fields (the other permitted solver field form)."""
    root = ET.parse(path).getroot()
    def arrays(parent_name: str) -> list[ET.Element]:
        parent = next((e for e in root.iter() if e.tag.endswith(parent_name)), None)
        return [] if parent is None else [e for e in parent if e.tag.endswith("DataArray")]
    points_array = arrays("Points")[0]
    points = np.fromstring(points_array.text or "", sep=" ", dtype=float).reshape(-1, int(points_array.get("NumberOfComponents", "3")))
    cell_arrays = {a.get("Name", "").lower(): np.fromstring(a.text or "", sep=" ", dtype=int) for a in arrays("Cells")}
    connectivity, offsets, types = cell_arrays.get("connectivity", np.array([], dtype=int)), cell_arrays.get("offsets", np.array([], dtype=int)), cell_arrays.get("types", np.array([], dtype=int))
    triangles: list[list[int]] = []; start = 0
    for offset, typ in zip(offsets, types):
        cell = connectivity[start:offset]; start = offset
        if typ == 5 and len(cell) == 3: triangles.append(cell.tolist())
        elif typ == 9 and len(cell) == 4: triangles.extend([cell[:3].tolist(), [int(cell[0]), int(cell[2]), int(cell[3])]])
    fields = {a.get("Name", "").lower(): np.fromstring(a.text or "", sep=" ", dtype=float) for a in arrays("PointData")}
    return points[:, :2], np.asarray(triangles, dtype=int), fields


def read_field(path: Path, variable: str) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    if path.suffix.lower() == ".csv":
        rows = csv_rows(path); names = {k.lower(): k for k in rows[0]}
        x, y = numeric(rows, names["x"]), numeric(rows, names["y"])
        key = names.get(variable.lower())
        if not key: raise KeyError(f"{path} lacks {variable}; has {sorted(names)}")
        tri = mtri.Triangulation(x, y)
        return x, y, tri.triangles, numeric(rows, key)
    points, triangles, fields = parse_vtu(path) if path.suffix.lower() == ".vtu" else parse_legacy_vtk(path)
    aliases = {"mach": ("mach", "mach_number"), "pressure": ("pressure", "p"), "vorticity": ("vorticity", "omega"), "velocity_magnitude": ("velocity_magnitude", "speed", "velocity")}
    values = next((fields[k] for k in aliases.get(variable, (variable,)) if k in fields), None)
    if values is None: raise KeyError(f"{path} lacks {variable}; has {sorted(fields)}")
    if len(values) != len(points):
        if not len(triangles): raise ValueError("cell-centered VTK field needs triangular CELLS")
        accum, hits = np.zeros(len(points)), np.zeros(len(points))
        for cell, value in zip(triangles, values): accum[cell] += value; hits[cell] += 1
        values = accum / np.maximum(hits, 1)
    return points[:,0], points[:,1], triangles, values


def plot_field(case: Path, variable: str, source: Path, output: Path, xlim: tuple[float, float] | None = None, ylim: tuple[float, float] | None = None) -> None:
    x, y, triangles, value = read_field(source, variable)
    tri = mtri.Triangulation(x, y, triangles) if len(triangles) else mtri.Triangulation(x, y)
    valid = value[np.isfinite(value)]
    if not len(valid): raise ValueError("field has no finite values")
    lo, hi = np.percentile(valid, [1, 99])
    if variable == "vorticity":
        scale = max(abs(float(np.percentile(valid, 2))), abs(float(np.percentile(valid, 98))), 1.0e-8)
        lo, hi = -scale, scale
    if math.isclose(lo, hi): lo, hi = valid.min() - .5, valid.max() + .5
    fig, ax = plt.subplots(figsize=(7.2, 4.7), constrained_layout=True)
    contour = ax.tricontourf(tri, value, levels=80, cmap="coolwarm" if variable == "vorticity" else "viridis", vmin=lo, vmax=hi)
    ax.triplot(tri, color="k", linewidth=.05, alpha=.08)
    if xlim: ax.set_xlim(*xlim)
    if ylim: ax.set_ylim(*ylim)
    ax.set_aspect("equal", adjustable="box"); ax.set(title=f"{case.name}: {variable}", xlabel="$x/L_{ref}$", ylabel="$y/L_{ref}$")
    colorbar = fig.colorbar(contour, ax=ax); colorbar.set_label(variable.replace("_", " "))
    fig.savefig(output, dpi=210)
    plt.close(fig)


def entry(case_id: str, output: Path, typ: str, variable: str, source: Path, view: str = "full domain") -> dict[str, str]:
    if typ == "residuals":
        description = "line plot of L2 and L-infinity global residuals"
    elif typ == "forces":
        description = "line plot of drag (CD) and lift (CL) coefficients"
    elif typ == "surface":
        description = "line plot of pressure (Cp) and skin-friction (Cf) surface coefficients"
    elif typ == "field" or typ == "field_zoom":
        description = f"color-filled {variable.replace('_', ' ')} contour"
    else:
        description = variable.replace("_", " ")
    return {"figure_file": output.name, "case_id": case_id, "figure_type": typ, "variable": variable, "source_file": str(source), "caption": f"{case_id}: {description} ({view}) derived from {source.name}."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path, nargs="?", default=ROOT / "results")
    parser.add_argument("--figures", type=Path, default=FIGURES)
    parser.add_argument("--manifest", type=Path, default=ROOT / "report" / "figure_manifest.csv")
    args = parser.parse_args(); args.figures.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, str]] = []
    if not args.results.exists():
        args.results.mkdir(parents=True)
    # Audit/probe outputs are intentionally retained beside the final result
    # packages; only the supplied benchmark case IDs belong in this manifest.
    for case in sorted(p for p in args.results.iterdir() if p.is_dir() and p.name in EXPECTED_CASE_IDS):
        cid = case.name
        for stem, columns, ylabel, log in [("residuals", [("residual_l2", "L2 residual"), ("residual_linf", "L∞ residual")], "global residual", True), ("forces", [("cd", "$C_D$"), ("cl", "$C_L$")], "force coefficient", False)]:
            source = case / f"{stem}.csv"
            if source.exists():
                rows = csv_rows(source)
                out = args.figures / f"{cid}_{stem}.png"
                save_line(out, numeric(rows, "physical_time" if any(float(r.get("physical_time", 0)) for r in rows) else "step"), [(label, numeric(rows, column)) for column, label in columns], "physical time" if any(float(r.get("physical_time", 0)) for r in rows) else "step", ylabel, f"{cid}: {stem}", log)
                manifest.append(entry(cid, out, stem, stem, source))
        surface = case / "surface.csv"
        if surface.exists():
            rows = csv_rows(surface); out = args.figures / f"{cid}_surface_cp.png"
            save_line(out, np.arange(len(rows)), [("$C_p$", numeric(rows, "cp")), ("$C_f$", numeric(rows, "cf"))], "wall sample index", "coefficient", f"{cid}: surface coefficients")
            manifest.append(entry(cid, out, "surface", "pressure coefficient", surface))
        fields = sorted(case.glob("field_final.*"))
        if fields:
            source = fields[0]
            is_cylinder = "cylinder" in cid.lower()
            zoom = ((-0.2, 1.2), (-0.2, 0.2)) if not is_cylinder else ((-1.0, 8.0), (-3.0, 3.0))
            for variable in ["mach", "pressure"] + (["vorticity", "velocity_magnitude"] if "re200" in cid.lower() else []):
                out = args.figures / f"{cid}_{variable}.png"
                try:
                    plot_field(case, variable, source, out)
                    manifest.append(entry(cid, out, "field", variable, source))
                    zoom_out = args.figures / f"{cid}_{variable}_zoom.png"
                    plot_field(case, variable, source, zoom_out, *zoom)
                    manifest.append(entry(cid, zoom_out, "field_zoom", variable, source, "body/wake zoom"))
                except (KeyError, ValueError, IndexError):
                    if out.exists(): out.unlink()
    with args.manifest.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=MANIFEST_COLUMNS, lineterminator="\n"); writer.writeheader(); writer.writerows(manifest)
    print(f"wrote {len(manifest)} figure entries to {args.manifest}")


if __name__ == "__main__": main()

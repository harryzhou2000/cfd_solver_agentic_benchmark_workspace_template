#!/usr/bin/env python3
"""Plot submitted CFD output files without manufacturing any solver data.

Supported field formats are ASCII legacy VTK ``UNSTRUCTURED_GRID`` files and
ASCII XML VTU ``UnstructuredGrid`` files.  Binary/appended VTK data is rejected
deliberately so a report build cannot quietly plot undecoded bytes.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


class PlotError(RuntimeError):
    """Raised when a submitted result cannot be plotted faithfully."""


@dataclass
class FieldMesh:
    points: np.ndarray
    cells: list[np.ndarray]
    point_data: dict[str, np.ndarray]
    cell_data: dict[str, np.ndarray]


RESIDUAL_COLUMNS = (
    "step", "physical_time", "inner_iter", "cfl", "dt", "rho", "rhou",
    "rhov", "rhoE", "residual_l2", "residual_linf",
)
FORCE_COLUMNS = (
    "step", "physical_time", "cl", "cd", "cmz", "pressure_drag",
    "viscous_drag", "pressure_lift", "viscous_lift",
)
SURFACE_COLUMNS = ("x", "y", "nx", "ny", "pressure", "cp", "cf", "rho", "u", "v", "mach", "tag")


def _normalise(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def _float_tokens(lines: Iterable[str], count: int, what: str) -> np.ndarray:
    values: list[float] = []
    for line in lines:
        values.extend(float(x) for x in line.split())
        if len(values) >= count:
            break
    if len(values) != count:
        raise PlotError(f"{what}: expected {count} numeric values, found {len(values)}")
    data = np.asarray(values, dtype=float)
    if not np.isfinite(data).all():
        raise PlotError(f"{what}: contains non-finite values")
    return data


def _parse_legacy_vtk(path: Path) -> FieldMesh:
    lines = path.read_text(encoding="utf-8", errors="strict").splitlines()
    if len(lines) < 4 or "ASCII" not in lines[2].upper() or "UNSTRUCTURED_GRID" not in " ".join(lines[:8]).upper():
        raise PlotError(f"{path}: only ASCII legacy VTK UNSTRUCTURED_GRID is supported")
    i = 0
    points: np.ndarray | None = None
    cells: list[np.ndarray] = []
    point_data: dict[str, np.ndarray] = {}
    cell_data: dict[str, np.ndarray] = {}
    active: dict[str, np.ndarray] | None = None
    tuple_count = 0
    while i < len(lines):
        words = lines[i].split()
        upper = [word.upper() for word in words]
        if not words:
            i += 1
            continue
        if upper[0] == "POINTS" and len(words) >= 3:
            n = int(words[1])
            values = _float_tokens(lines[i + 1 :], n * 3, f"{path} POINTS")
            points = values.reshape(n, 3)[:, :2]
            # Counts are easier and safer to advance by scanning token totals.
            seen = 0
            i += 1
            while seen < n * 3:
                seen += len(lines[i].split())
                i += 1
            continue
        if upper[0] == "CELLS" and len(words) >= 3:
            n_cells, n_values = int(words[1]), int(words[2])
            values = _float_tokens(lines[i + 1 :], n_values, f"{path} CELLS").astype(int)
            cursor = 0
            for _ in range(n_cells):
                n = int(values[cursor])
                cursor += 1
                cell = values[cursor : cursor + n]
                if len(cell) != n:
                    raise PlotError(f"{path}: truncated cell connectivity")
                cells.append(cell)
                cursor += n
            if cursor != n_values:
                raise PlotError(f"{path}: CELLS declaration has unused values")
            seen = 0
            i += 1
            while seen < n_values:
                seen += len(lines[i].split())
                i += 1
            continue
        if upper[0] in {"POINT_DATA", "CELL_DATA"} and len(words) == 2:
            tuple_count = int(words[1])
            active = point_data if upper[0] == "POINT_DATA" else cell_data
            i += 1
            continue
        if upper[0] == "SCALARS" and active is not None and len(words) >= 3:
            name = words[1]
            components = int(words[3]) if len(words) >= 4 else 1
            if i + 1 >= len(lines) or not lines[i + 1].upper().startswith("LOOKUP_TABLE"):
                raise PlotError(f"{path}: SCALARS {name} has no LOOKUP_TABLE")
            count = tuple_count * components
            values = _float_tokens(lines[i + 2 :], count, f"{path} scalar {name}")
            active[name] = values.reshape(tuple_count, components)
            seen, i = 0, i + 2
            while seen < count:
                seen += len(lines[i].split())
                i += 1
            continue
        if upper[0] == "VECTORS" and active is not None and len(words) >= 3:
            name, count = words[1], tuple_count * 3
            values = _float_tokens(lines[i + 1 :], count, f"{path} vector {name}")
            active[name] = values.reshape(tuple_count, 3)
            seen, i = 0, i + 1
            while seen < count:
                seen += len(lines[i].split())
                i += 1
            continue
        i += 1
    if points is None or not cells:
        raise PlotError(f"{path}: missing POINTS or CELLS")
    return FieldMesh(points, cells, point_data, cell_data)


def _parse_xml_data_array(node: ET.Element, path: Path) -> np.ndarray:
    if node.attrib.get("format", "ascii").lower() != "ascii":
        raise PlotError(f"{path}: DataArray {node.attrib.get('Name', '')!r} is not ASCII")
    raw = (node.text or "").strip()
    if not raw:
        raise PlotError(f"{path}: empty DataArray {node.attrib.get('Name', '')!r}")
    data = np.fromstring(raw, sep=" ", dtype=float)
    if not data.size or not np.isfinite(data).all():
        raise PlotError(f"{path}: invalid DataArray {node.attrib.get('Name', '')!r}")
    return data


def _parse_vtu(path: Path) -> FieldMesh:
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise PlotError(f"{path}: invalid XML VTU") from exc
    piece = root.find(".//Piece")
    if piece is None:
        raise PlotError(f"{path}: missing UnstructuredGrid Piece")
    n_points, n_cells = int(piece.attrib["NumberOfPoints"]), int(piece.attrib["NumberOfCells"])
    point_array = piece.find("./Points/DataArray")
    if point_array is None:
        raise PlotError(f"{path}: missing Points DataArray")
    point_values = _parse_xml_data_array(point_array, path)
    components = int(point_array.attrib.get("NumberOfComponents", "3"))
    if components < 2 or point_values.size != n_points * components:
        raise PlotError(f"{path}: invalid point array dimensions")
    points = point_values.reshape(n_points, components)[:, :2]
    arrays = {node.attrib.get("Name", ""): _parse_xml_data_array(node, path) for node in piece.findall("./Cells/DataArray")}
    if not {"connectivity", "offsets"}.issubset(arrays):
        raise PlotError(f"{path}: Cells needs connectivity and offsets arrays")
    conn, offsets = arrays["connectivity"].astype(int), arrays["offsets"].astype(int)
    if len(offsets) != n_cells:
        raise PlotError(f"{path}: cell offset count does not match NumberOfCells")
    cells, previous = [], 0
    for end in offsets:
        cell = conn[previous:end]
        if len(cell) < 3:
            raise PlotError(f"{path}: non-area cell cannot be plotted")
        cells.append(cell)
        previous = int(end)
    if previous != len(conn):
        raise PlotError(f"{path}: cell offsets do not consume connectivity")

    def read_attributes(section: str, expected: int) -> dict[str, np.ndarray]:
        values: dict[str, np.ndarray] = {}
        for node in piece.findall(f"./{section}/DataArray"):
            name = node.attrib.get("Name")
            if not name:
                continue
            components = int(node.attrib.get("NumberOfComponents", "1"))
            data = _parse_xml_data_array(node, path)
            if data.size != expected * components:
                raise PlotError(f"{path}: {section} array {name} has wrong length")
            values[name] = data.reshape(expected, components)
        return values

    return FieldMesh(points, cells, read_attributes("PointData", n_points), read_attributes("CellData", n_cells))


def read_field(path: Path) -> FieldMesh:
    if path.suffix.lower() == ".vtu":
        return _parse_vtu(path)
    if path.suffix.lower() == ".vtk":
        return _parse_legacy_vtk(path)
    raise PlotError(f"{path}: supported final field formats are ASCII .vtu and .vtk")


def _field(mesh: FieldMesh, names: tuple[str, ...], component: int = 0) -> np.ndarray:
    wanted = {_normalise(name) for name in names}
    for source in (mesh.point_data, mesh.cell_data):
        for name, values in source.items():
            if _normalise(name) in wanted:
                if values.ndim != 2 or values.shape[1] <= component:
                    raise PlotError(f"field {name!r} lacks component {component}")
                selected = values[:, component]
                if source is mesh.point_data:
                    return selected
                accum, count = np.zeros(len(mesh.points)), np.zeros(len(mesh.points))
                for value, cell in zip(selected, mesh.cells, strict=True):
                    accum[cell] += value
                    count[cell] += 1
                if np.any(count == 0):
                    raise PlotError(f"cell field {name!r} cannot be mapped to isolated points")
                return accum / count
    raise PlotError(f"missing required field; expected one of {', '.join(names)}")


def _velocity(mesh: FieldMesh) -> tuple[np.ndarray, np.ndarray]:
    for source in (mesh.point_data, mesh.cell_data):
        for name, values in source.items():
            if _normalise(name) in {"velocity", "vel"} and values.shape[1] >= 2:
                if source is mesh.point_data:
                    return values[:, 0], values[:, 1]
                # Reuse the scalar conversion path by giving temporary names.
                old_u, old_v = mesh.cell_data.get("__u"), mesh.cell_data.get("__v")
                mesh.cell_data["__u"], mesh.cell_data["__v"] = values[:, :1], values[:, 1:2]
                try:
                    return _field(mesh, ("__u",)), _field(mesh, ("__v",))
                finally:
                    if old_u is None:
                        mesh.cell_data.pop("__u", None)
                    else:
                        mesh.cell_data["__u"] = old_u
                    if old_v is None:
                        mesh.cell_data.pop("__v", None)
                    else:
                        mesh.cell_data["__v"] = old_v
    return _field(mesh, ("u", "velocityx", "velx")), _field(mesh, ("v", "velocityy", "vely"))


def _triangulation(mesh: FieldMesh) -> mtri.Triangulation:
    triangles: list[list[int]] = []
    for cell in mesh.cells:
        for i in range(1, len(cell) - 1):
            triangles.append([int(cell[0]), int(cell[i]), int(cell[i + 1])])
    if not triangles:
        raise PlotError("field mesh has no triangular area faces")
    return mtri.Triangulation(mesh.points[:, 0], mesh.points[:, 1], np.asarray(triangles, dtype=int))


def _read_csv(path: Path, required: tuple[str, ...]) -> list[dict[str, float | str]]:
    if not path.is_file():
        raise PlotError(f"missing {path}")
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != list(required):
            raise PlotError(f"{path}: exact header required: {','.join(required)}")
        rows = list(reader)
    if not rows:
        raise PlotError(f"{path}: no data rows")
    result: list[dict[str, float | str]] = []
    for row_number, row in enumerate(rows, 2):
        out: dict[str, float | str] = {}
        for key in required:
            if key == "tag":
                out[key] = row[key]
            else:
                try:
                    out[key] = float(row[key])
                except (TypeError, ValueError) as exc:
                    raise PlotError(f"{path}:{row_number}: {key} is not numeric") from exc
                if not math.isfinite(out[key]):
                    raise PlotError(f"{path}:{row_number}: {key} is not finite")
        result.append(out)
    return result


def read_case_tables(case_dir: Path) -> tuple[list[dict[str, float | str]], list[dict[str, float | str]], list[dict[str, float | str]]]:
    return (
        _read_csv(case_dir / "residuals.csv", RESIDUAL_COLUMNS),
        _read_csv(case_dir / "forces.csv", FORCE_COLUMNS),
        _read_csv(case_dir / "surface.csv", SURFACE_COLUMNS),
    )


def _save(fig: plt.Figure, path: Path) -> None:
    fig.tight_layout()
    fig.savefig(path, dpi=180, bbox_inches="tight")
    plt.close(fig)


def _line_plot(x: np.ndarray, series: list[tuple[str, np.ndarray]], path: Path,
               ylabel: str, logy: bool = False, xlabel: str = "step") -> None:
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    for label, values in series:
        ax.plot(x, values, linewidth=1.5, label=label)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    if logy:
        if np.any(np.concatenate([item[1] for item in series]) <= 0):
            raise PlotError(f"{path}: logarithmic residual plot requires positive residuals")
        ax.set_yscale("log")
    ax.grid(True, alpha=0.3)
    ax.legend()
    _save(fig, path)


def _field_plot(mesh: FieldMesh, values: np.ndarray, title: str, colorbar_label: str,
                path: Path, xlim: tuple[float, float], ylim: tuple[float, float]) -> tuple[float, float]:
    tri = _triangulation(mesh)
    fig, ax = plt.subplots(figsize=(6.4, 4.8))
    in_view = ((mesh.points[:, 0] >= xlim[0]) & (mesh.points[:, 0] <= xlim[1]) &
               (mesh.points[:, 1] >= ylim[0]) & (mesh.points[:, 1] <= ylim[1]))
    scale_values = values[in_view] if np.count_nonzero(in_view) >= 10 else values
    vmin, vmax = np.percentile(scale_values, [1.0, 99.0])
    if not vmax > vmin:
        vmin, vmax = float(np.min(scale_values)), float(np.max(scale_values))
    if not vmax > vmin:
        vmax = vmin + max(abs(vmin), 1.0) * 1e-12
    image = ax.tripcolor(tri, values, shading="gouraud", cmap="viridis",
                         vmin=float(vmin), vmax=float(vmax))
    fig.colorbar(image, ax=ax, label=colorbar_label)
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title(title)
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    _save(fig, path)
    return float(vmin), float(vmax)


def _vorticity_plot(mesh: FieldMesh, u: np.ndarray, v: np.ndarray, path: Path,
                    xlim: tuple[float, float], ylim: tuple[float, float]) -> float:
    tri = _triangulation(mesh)
    triangles = tri.triangles
    x, y = mesh.points[:, 0], mesh.points[:, 1]
    vorticity = np.empty(len(triangles))
    for index, (a, b, c) in enumerate(triangles):
        matrix = np.array([[x[b] - x[a], y[b] - y[a]], [x[c] - x[a], y[c] - y[a]]])
        if abs(np.linalg.det(matrix)) < 1e-14:
            raise PlotError("degenerate triangle prevents vorticity calculation")
        du_dx, du_dy = np.linalg.solve(matrix, np.array([u[b] - u[a], u[c] - u[a]]))
        dv_dx, _dv_dy = np.linalg.solve(matrix, np.array([v[b] - v[a], v[c] - v[a]]))
        vorticity[index] = dv_dx - du_dy
    limit = min(max(float(np.percentile(np.abs(vorticity), 99.0)), 1e-12), 5.0)
    fig, ax = plt.subplots(figsize=(6.4, 4.8))
    image = ax.tripcolor(tri, facecolors=vorticity, shading="flat", cmap="coolwarm", vmin=-limit, vmax=limit)
    fig.colorbar(image, ax=ax, label="vorticity (derived from submitted velocity)")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    ax.set_title("Wake vorticity")
    ax.set_aspect("equal", adjustable="box")
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    _save(fig, path)
    return limit


def lift_spectrum(time: np.ndarray, lift: np.ndarray) -> dict[str, np.ndarray | float]:
    """Return a detrended, one-sided lift spectrum for uniformly sampled data.

    The report builder validates cadence before using this helper.  Refusing to
    interpolate here keeps the reported frequency traceable to force samples.
    """
    if len(time) < 64 or len(time) != len(lift):
        raise PlotError("lift spectrum requires at least 64 matched force samples")
    intervals = np.diff(time)
    dt = float(np.median(intervals))
    if not dt > 0.0 or not np.allclose(intervals, dt, rtol=1.0e-6, atol=1.0e-10):
        raise PlotError("lift spectrum requires uniformly sampled physical time")
    coefficients = np.polyfit(time - time[0], lift, 1)
    signal = lift - np.polyval(coefficients, time - time[0])
    window = np.hanning(len(signal))
    amplitude = np.abs(np.fft.rfft(signal * window))
    frequency = np.fft.rfftfreq(len(signal), d=dt)
    if len(frequency) < 3 or not np.any(amplitude[1:] > 0.0):
        raise PlotError("lift spectrum has no nonzero non-DC content")
    peak_index = 1 + int(np.argmax(amplitude[1:]))
    competitors = np.delete(amplitude[1:], peak_index - 1)
    second = float(np.max(competitors)) if competitors.size else 0.0
    peak = float(amplitude[peak_index])
    return {
        "frequency": frequency,
        "amplitude": amplitude,
        "sample_dt": dt,
        "window_duration": float(time[-1] - time[0]),
        "frequency_resolution": float(frequency[1] - frequency[0]),
        "dominant_frequency": float(frequency[peak_index]),
        "peak_to_second_ratio": peak / max(second, 1.0e-300),
    }


def _lift_spectrum_plot(spectrum: dict[str, np.ndarray | float], case_id: str,
                        path: Path) -> float:
    frequency = np.asarray(spectrum["frequency"], dtype=float)
    amplitude = np.asarray(spectrum["amplitude"], dtype=float)
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    ax.plot(frequency[1:], amplitude[1:], linewidth=1.3, color="tab:purple")
    dominant = float(spectrum["dominant_frequency"])
    ax.axvline(dominant, color="tab:red", linestyle="--", linewidth=1.1,
               label=f"dominant {dominant:.5g}")
    resolution = float(spectrum["frequency_resolution"])
    plot_max = min(float(frequency[-1]), max(5.0 * dominant, 10.0 * resolution))
    ax.set_xlim(0.0, plot_max)
    ax.set_xlabel("frequency / $U_\\infty L_{ref}^{-1}$")
    ax.set_ylabel("windowed lift-spectrum magnitude")
    ax.set_title(f"{case_id}: post-transient lift spectrum")
    ax.grid(True, alpha=0.3)
    ax.legend()
    _save(fig, path)
    return plot_max


def _surface_plot(surface: list[dict[str, float | str]], case_id: str, path: Path) -> None:
    x = np.asarray([float(row["x"]) for row in surface])
    y = np.asarray([float(row["y"]) for row in surface])
    cp = np.asarray([float(row["cp"]) for row in surface])
    cf = np.asarray([float(row["cf"]) for row in surface])
    fig, (pressure_ax, friction_ax) = plt.subplots(2, 1, figsize=(6.4, 6.2), sharex=True)
    if "cylinder" in case_id.lower():
        coordinate = np.degrees(np.arctan2(y, x))
        order = np.argsort(coordinate)
        pressure_ax.plot(coordinate[order], cp[order], linewidth=1.5, label="$C_p$")
        friction_ax.plot(coordinate[order], cf[order], linewidth=1.5, label="$C_f$")
        xlabel = "surface angle (degrees)"
    else:
        upper = y >= 0.0
        lower = ~upper
        for mask, label in ((upper, "upper"), (lower, "lower")):
            order = np.argsort(x[mask])
            pressure_ax.plot(x[mask][order], cp[mask][order], linewidth=1.5,
                             label=f"$C_p$ {label}")
            friction_ax.plot(x[mask][order], cf[mask][order], linewidth=1.5,
                             label=f"$C_f$ {label}")
        pressure_ax.invert_yaxis()
        xlabel = "$x/L_{ref}$"
    for axis, ylabel in ((pressure_ax, "$C_p$"), (friction_ax, "$C_f$")):
        axis.set_ylabel(ylabel)
        axis.grid(True, alpha=0.3)
        axis.legend()
    friction_ax.set_xlabel(xlabel)
    _save(fig, path)


def final_field_path(case_dir: Path) -> Path:
    candidates = sorted([*case_dir.glob("field_final.vtu"), *case_dir.glob("field_final.vtk")])
    if len(candidates) != 1:
        raise PlotError(f"{case_dir}: expected exactly one supported field_final.vtu or field_final.vtk")
    return candidates[0]


def generate_case_figures(case_dir: Path, figures_dir: Path) -> list[dict[str, str]]:
    """Generate required plots using only the submitted case directory."""
    case_id = case_dir.name
    figures_dir.mkdir(parents=True, exist_ok=True)
    residuals, forces, surface = read_case_tables(case_dir)
    field_path = final_field_path(case_dir)
    mesh = read_field(field_path)
    physical_time = np.asarray([float(row["physical_time"]) for row in residuals])
    force_time = np.asarray([float(row["physical_time"]) for row in forces])
    residual_axis = physical_time if np.ptp(physical_time) > 0 else np.asarray([float(row["step"]) for row in residuals])
    force_axis = force_time if np.ptp(force_time) > 0 else np.asarray([float(row["step"]) for row in forces])
    residual_xlabel = "physical time" if np.ptp(physical_time) > 0 else "pseudo step"
    force_xlabel = "physical time" if np.ptp(force_time) > 0 else "pseudo step"
    records: list[dict[str, str]] = []

    def add(filename: str, figure_type: str, variable: str, source: str, caption: str) -> Path:
        records.append({"figure_file": filename, "case_id": case_id, "figure_type": figure_type,
                        "variable": variable, "source_file": source, "caption": caption})
        return figures_dir / filename

    _line_plot(residual_axis, [("L2", np.asarray([float(row["residual_l2"]) for row in residuals])),
                               ("Linf", np.asarray([float(row["residual_linf"]) for row in residuals]))],
               add(f"{case_id}_residuals.png", "line_plot", "residual", "residuals.csv", "Global residual history."), "global residual", logy=True, xlabel=residual_xlabel)
    _line_plot(force_axis, [("$C_L$", np.asarray([float(row["cl"]) for row in forces])),
                            ("$C_D$", np.asarray([float(row["cd"]) for row in forces]))],
               add(f"{case_id}_forces.png", "line_plot", "force_coefficients", "forces.csv", "Force-coefficient history."), "force coefficient", xlabel=force_xlabel)
    _surface_plot(surface, case_id,
                  add(f"{case_id}_surface_cp.png", "line_plot", "surface_cp", "surface.csv", "Submitted surface pressure and skin-friction coefficients."))
    if "naca" in case_id.lower():
        xlim, ylim = (-0.1, 1.1), (-0.35, 0.35)
    else:
        xlim, ylim = (-2.0, 8.0), (-3.0, 3.0)
    mach_path = add(f"{case_id}_mach.png", "filled_unstructured_field", "mach", field_path.name, "")
    mach_range = _field_plot(mesh, _field(mesh, ("mach", "machnumber")), f"{case_id}: Mach number", "Mach number",
                             mach_path, xlim, ylim)
    records[-1]["caption"] = ("Mach number from the final submitted field; the displayed "
                              f"percentile-clipped range is [{mach_range[0]:.5g}, {mach_range[1]:.5g}].")
    pressure_path = add(f"{case_id}_pressure.png", "filled_unstructured_field", "pressure", field_path.name, "")
    pressure_range = _field_plot(mesh, _field(mesh, ("pressure", "p")), f"{case_id}: Pressure", "pressure",
                                 pressure_path, xlim, ylim)
    records[-1]["caption"] = ("Pressure from the final submitted field; the displayed "
                              f"percentile-clipped range is [{pressure_range[0]:.5g}, {pressure_range[1]:.5g}].")
    if "re200" in case_id.lower():
        u, v = _velocity(mesh)
        vorticity_path = add(f"{case_id}_vorticity.png", "filled_unstructured_field", "vorticity", field_path.name, "")
        limit = _vorticity_plot(mesh, u, v, vorticity_path, (-2.0, 12.0), (-4.0, 4.0))
        records[-1]["caption"] = ("Vorticity derived from final submitted velocity components; "
                                  f"symmetric colour clipping is [{-limit:.5g}, {limit:.5g}].")
        spectrum_start = force_time[0] + 0.8 * (force_time[-1] - force_time[0])
        spectrum_mask = force_time >= spectrum_start
        spectrum = lift_spectrum(force_time[spectrum_mask],
                                 np.asarray([float(row["cl"]) for row in forces])[spectrum_mask])
        spectrum_path = add(f"{case_id}_lift_spectrum.png", "line_plot", "lift_spectrum", "forces.csv", "")
        spectrum_plot_max = _lift_spectrum_plot(spectrum, case_id, spectrum_path)
        records[-1]["caption"] = (
            "Post-transient detrended Hann-window lift spectrum from forces.csv; "
            f"sample interval {float(spectrum['sample_dt']):.5g}, window {float(spectrum['window_duration']):.5g}, "
            f"frequency resolution {float(spectrum['frequency_resolution']):.5g}, dominant frequency "
            f"{float(spectrum['dominant_frequency']):.5g}, peak/second ratio "
            f"{float(spectrum['peak_to_second_ratio']):.3g}. The displayed low-frequency window ends at "
            f"{spectrum_plot_max:.5g}; the FFT was evaluated through the Nyquist frequency "
            f"{float(np.asarray(spectrum['frequency'])[-1]):.5g}.")
    return records


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("case_dir", type=Path)
    parser.add_argument("--figures-dir", required=True, type=Path)
    args = parser.parse_args()
    try:
        records = generate_case_figures(args.case_dir, args.figures_dir)
    except PlotError as exc:
        raise SystemExit(f"plot_results: {exc}") from exc
    for record in records:
        print(record["figure_file"])


if __name__ == "__main__":
    main()

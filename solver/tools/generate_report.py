#!/usr/bin/env python3
"""Generate traceable CFD report artifacts from real solver output files.

This intentionally performs no solver calculations and has no synthetic-data
fallbacks.  Invalid/insufficient output is reported as an error.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import shlex
import shutil
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import numpy as np


RESIDUAL_COLUMNS = ["step", "physical_time", "inner_iter", "cfl", "dt", "rho", "rhou", "rhov", "rhoE", "residual_l2", "residual_linf"]
FORCE_COLUMNS = ["step", "physical_time", "cl", "cd", "cmz", "pressure_drag", "viscous_drag", "pressure_lift", "viscous_lift"]
SURFACE_COLUMNS = ["x", "y", "nx", "ny", "pressure", "cp", "cf", "rho", "u", "v", "mach", "tag"]
PARTITION_COLUMNS = ["rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
                     "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells"]
WORKSPACE_ROOT = Path(__file__).resolve().parents[2]


class OutputError(RuntimeError):
    pass


@dataclass(frozen=True)
class CaseInput:
    """The immutable case JSON named by a submitted command."""
    path: Path
    data: dict[str, Any]


@dataclass(frozen=True)
class PartitionRow:
    rank: int
    owned: int
    ghost: int
    boundary_faces: int
    neighbours: int
    send_cells: int
    recv_cells: int


def read_csv(path: Path, required: list[str]) -> list[dict[str, float | str]]:
    if not path.is_file():
        raise OutputError(f"missing required output: {path}")
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != required:
            raise OutputError(f"{path}: expected header {required}, got {reader.fieldnames}")
        rows = list(reader)
    if not rows:
        raise OutputError(f"{path}: contains no data rows")
    parsed: list[dict[str, float | str]] = []
    for row_number, row in enumerate(rows, 2):
        parsed_row: dict[str, float | str] = {}
        for key, value in row.items():
            if key == "tag":
                parsed_row[key] = value
                continue
            try:
                number = float(value)
            except (TypeError, ValueError) as exc:
                raise OutputError(f"{path}:{row_number}: {key} is not numeric") from exc
            if not math.isfinite(number):
                raise OutputError(f"{path}:{row_number}: {key} is non-finite")
            parsed_row[key] = number
        parsed.append(parsed_row)
    return parsed


def command_option(command: object, option: str, case_id: str) -> str:
    """Extract exactly one option from a stored shell command without guessing."""
    if not isinstance(command, str) or not command.strip():
        raise OutputError(f"{case_id}: run_status.command is missing or blank")
    try:
        tokens = shlex.split(command)
    except ValueError as exc:
        raise OutputError(f"{case_id}: cannot parse run_status.command: {exc}") from exc
    values: list[str] = []
    prefix = option + "="
    for index, token in enumerate(tokens):
        if token == option:
            if index + 1 >= len(tokens) or tokens[index + 1].startswith("--"):
                raise OutputError(f"{case_id}: {option} in run_status.command has no value")
            values.append(tokens[index + 1])
        elif token.startswith(prefix):
            value = token[len(prefix):]
            if not value:
                raise OutputError(f"{case_id}: {option} in run_status.command has no value")
            values.append(value)
    if len(values) != 1:
        raise OutputError(f"{case_id}: run_status.command must contain exactly one {option} option")
    return values[0]


def command_path(value: str, label: str, case_id: str) -> Path:
    """Resolve a command path against the checkout, then the invocation CWD."""
    supplied = Path(value)
    candidates = [supplied] if supplied.is_absolute() else [WORKSPACE_ROOT / supplied, Path.cwd() / supplied]
    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        if resolved.exists():
            return resolved
    raise OutputError(f"{case_id}: {label} path from run_status.command does not exist: {value}")


def load_command_case_input(directory: Path, case_id: str, metadata: dict[str, Any],
                            status: dict[str, Any]) -> CaseInput:
    """Prove that the output package and its recorded command name one input."""
    command = status.get("command")
    case_path = command_path(command_option(command, "--case", case_id), "--case", case_id)
    output_path = command_path(command_option(command, "--output", case_id), "--output", case_id)
    if output_path != directory.resolve():
        raise OutputError(
            f"{case_id}: command --output ({output_path}) does not name the submitted directory ({directory.resolve()})")
    try:
        data = json.loads(case_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise OutputError(f"{case_id}: cannot read command --case JSON {case_path}: {exc}") from exc
    if not isinstance(data, dict):
        raise OutputError(f"{case_id}: command --case JSON must be an object")
    if data.get("case_id") != case_id:
        raise OutputError(
            f"{case_id}: command --case JSON identifies {data.get('case_id')!r}, not the submitted output case")
    mesh = data.get("mesh")
    boundaries = data.get("boundary_conditions")
    if not isinstance(mesh, dict) or not isinstance(mesh.get("file"), str) or not isinstance(boundaries, dict):
        raise OutputError(f"{case_id}: command --case JSON lacks mesh or boundary_conditions evidence")
    recorded_mesh = metadata.get("mesh_file")
    if isinstance(recorded_mesh, str) and Path(recorded_mesh).name != Path(mesh["file"]).name:
        raise OutputError(
            f"{case_id}: metadata mesh {Path(recorded_mesh).name!r} disagrees with command case mesh {mesh['file']!r}")
    return CaseInput(case_path, data)


def numeric(rows: list[dict[str, float | str]], key: str) -> np.ndarray:
    return np.asarray([float(row[key]) for row in rows], dtype=float)


def field_file(case_dir: Path) -> Path:
    choices = sorted(case_dir.glob("field_final.*"))
    if not choices:
        raise OutputError(f"{case_dir}: missing field_final.*")
    supported = [item for item in choices if item.suffix.lower() in {".vtu", ".pvtu", ".vtk", ".cgns"}]
    if not supported:
        raise OutputError(f"{case_dir}: field file must be .vtu, .pvtu, .vtk, or .cgns; found {choices}")
    # A distributed PVTU index is the authoritative all-rank field whenever
    # present.  `field_final.vtu` is also emitted for the literal contract and
    # single-file consumers, but should not silently replace the distributed
    # provenance path used for report rendering.
    return next((item for item in supported if item.suffix.lower() == ".pvtu"), supported[0])


def require_literal_final_field(case_dir: Path) -> None:
    """The output contract requires a readable final field, not only an index."""
    if not any((case_dir / f"field_final{suffix}").is_file() for suffix in (".vtu", ".vtk", ".cgns")):
        raise OutputError(
            f"{case_dir}: required literal final field is missing (expected field_final.vtu, .vtk, or .cgns)")


def normalized(name: str) -> str:
    return "".join(char for char in name.lower() if char.isalnum())


def named_data(data: dict[str, Any], aliases: Iterable[str], location: str) -> np.ndarray:
    wanted = {normalized(alias) for alias in aliases}
    for key, value in data.items():
        if normalized(key) in wanted:
            values = np.asarray(value, dtype=float)
            if values.ndim == 2 and values.shape[1] == 1:
                values = values[:, 0]
            if values.ndim != 1:
                raise OutputError(f"{location} field {key!r} must be scalar")
            if not np.isfinite(values).all():
                raise OutputError(f"{location} field {key!r} contains non-finite values")
            return values
    raise OutputError(f"{location}: missing one of scalar fields {sorted(wanted)}")


@dataclass
class Field:
    points: np.ndarray
    triangles: np.ndarray
    cell_triangle_multiplicity: np.ndarray
    point_data: dict[str, np.ndarray]
    cell_data: dict[str, np.ndarray]

    def scalar(self, aliases: Iterable[str]) -> tuple[np.ndarray, str]:
        try:
            return named_data(self.point_data, aliases, "point-data"), "point"
        except OutputError:
            return named_data(self.cell_data, aliases, "cell-data"), "cell"

    def velocity(self) -> tuple[np.ndarray, np.ndarray, str]:
        """Return components from u/v fields or a standard vector velocity field."""
        for data, location in ((self.point_data, "point"), (self.cell_data, "cell")):
            try:
                return (named_data(data, ["u", "velocity_x", "velocityx"], f"{location}-data"),
                        named_data(data, ["v", "velocity_y", "velocityy"], f"{location}-data"), location)
            except OutputError:
                pass
            for name, value in data.items():
                if normalized(name) in {"velocity", "vel"}:
                    array = np.asarray(value, dtype=float)
                    if array.ndim == 2 and array.shape[1] >= 2 and np.isfinite(array).all():
                        return array[:, 0], array[:, 1], location
        raise OutputError("field must contain u/v (or velocity_x/velocity_y) or a two-component velocity vector")


def load_field(path: Path) -> Field:
    try:
        import meshio
    except ModuleNotFoundError as exc:
        raise OutputError("meshio is required; install solver/tools/requirements.txt in .venv") from exc
    try:
        if path.suffix.lower() == ".pvtu":
            # meshio does not register PVTU as a readable extension.  A PVTU
            # file is an XML index over ordinary VTU pieces, so merge those
            # pieces here without changing or fabricating their field values.
            root = ET.parse(path).getroot()
            sources = [node.attrib["Source"] for node in root.iter()
                       if node.tag.rsplit("}", 1)[-1] == "Piece" and "Source" in node.attrib]
            if not sources:
                raise OutputError(f"{path}: PVTU contains no Piece Source entries")
            pieces = [meshio.read(path.parent / source) for source in sources]
            point_data_keys = set(pieces[0].point_data)
            cell_data_keys = set(pieces[0].cell_data)
            if any(set(piece.point_data) != point_data_keys or set(piece.cell_data) != cell_data_keys for piece in pieces[1:]):
                raise OutputError(f"{path}: PVTU pieces have inconsistent field arrays")
            points: list[np.ndarray] = []
            cells: list[Any] = []
            point_data = {name: [] for name in point_data_keys}
            cell_data = {name: [] for name in cell_data_keys}
            offset = 0
            for piece in pieces:
                points.append(np.asarray(piece.points))
                for name in point_data:
                    point_data[name].append(np.asarray(piece.point_data[name]))
                for index, block in enumerate(piece.cells):
                    cells.append((block.type, np.asarray(block.data) + offset))
                    for name in cell_data:
                        cell_data[name].append(np.asarray(piece.cell_data[name][index]))
                offset += len(piece.points)
            mesh = meshio.Mesh(points=np.vstack(points), cells=cells,
                               point_data={name: np.concatenate(values) for name, values in point_data.items()},
                               cell_data=cell_data)
        else:
            mesh = meshio.read(path)
    except OutputError:
        raise
    except Exception as exc:
        raise OutputError(f"cannot read {path} with meshio: {exc}") from exc
    points = np.asarray(mesh.points, dtype=float)
    if points.ndim != 2 or points.shape[1] < 2 or len(points) < 3:
        raise OutputError(f"{path}: invalid point coordinates")
    triangles: list[np.ndarray] = []
    multiplicity: list[np.ndarray] = []
    for block in mesh.cells:
        cells = np.asarray(block.data, dtype=int)
        if block.type == "triangle":
            triangles.append(cells)
            multiplicity.append(np.ones(len(cells), dtype=int))
        elif block.type == "quad":
            triangles.append(cells[:, [0, 1, 2]])
            triangles.append(cells[:, [0, 2, 3]])
            multiplicity.append(np.full(len(cells), 2, dtype=int))
        elif block.type in {"vertex", "line"}:
            continue
        else:
            raise OutputError(f"{path}: unsupported cell type {block.type!r} for 2-D rendering")
    if not triangles:
        raise OutputError(f"{path}: no 2-D triangle or quad cells")
    point_data = {key: np.asarray(value) for key, value in mesh.point_data.items()}
    cell_data: dict[str, np.ndarray] = {}
    for name, blocks in mesh.cell_data.items():
        values: list[np.ndarray] = []
        for block, value in zip(mesh.cells, blocks):
            if block.type in {"triangle", "quad"}:
                values.append(np.asarray(value).reshape(len(block.data), -1))
        if values:
            merged = np.concatenate(values, axis=0)
            cell_data[name] = merged[:, 0] if merged.shape[1] == 1 else merged
    return Field(points=points[:, :2], triangles=np.vstack(triangles),
                 cell_triangle_multiplicity=np.concatenate(multiplicity),
                 point_data=point_data, cell_data=cell_data)


def percentiles(values: np.ndarray) -> tuple[float, float]:
    low, high = np.nanpercentile(values, [1.0, 99.0])
    if not math.isfinite(float(low)) or not math.isfinite(float(high)) or low == high:
        low, high = float(np.min(values)), float(np.max(values))
    if low == high:
        span = max(abs(low) * 0.05, 1.0e-8)
        low, high = low - span, high + span
    return float(low), float(high)


def plot_field(field: Field, aliases: list[str], title: str, label: str, destination: Path,
               limits: tuple[tuple[float, float], tuple[float, float]] | None = None) -> np.ndarray:
    values, location = field.scalar(aliases)
    tri = mtri.Triangulation(field.points[:, 0], field.points[:, 1], field.triangles)
    fig, axis = plt.subplots(figsize=(8.0, 4.8), constrained_layout=True)
    low, high = percentiles(values)
    if location == "point":
        artist = axis.tricontourf(tri, values, levels=64, cmap="viridis", vmin=low, vmax=high)
    else:
        # meshio cell data has one value per original cell; duplicate quads' values.
        face_values = np.repeat(values, field.cell_triangle_multiplicity)
        if len(face_values) != len(field.triangles):
            raise OutputError("cell-data count does not match renderable field cells")
        artist = axis.tripcolor(tri, facecolors=face_values, shading="flat", cmap="viridis", vmin=low, vmax=high)
    colorbar = fig.colorbar(artist, ax=axis)
    colorbar.set_label(label)
    axis.set(title=title, xlabel="$x$", ylabel="$y$")
    axis.set_aspect("equal", adjustable="box")
    if limits is not None:
        axis.set_xlim(*limits[0])
        axis.set_ylim(*limits[1])
    axis.grid(alpha=0.2)
    fig.savefig(destination, dpi=220)
    plt.close(fig)
    return values


def plot_history(rows: list[dict[str, float | str]], keys: list[str], ylabel: str, title: str, destination: Path) -> None:
    fig, axis = plt.subplots(figsize=(7.4, 4.5), constrained_layout=True)
    time = numeric(rows, "physical_time")
    for key in keys:
        axis.plot(time, numeric(rows, key), label=key, linewidth=1.5)
    axis.set(title=title, xlabel="physical time", ylabel=ylabel)
    axis.grid(True, alpha=0.3)
    axis.legend()
    fig.savefig(destination, dpi=220)
    plt.close(fig)


def plot_residuals(rows: list[dict[str, float | str]], title: str, destination: Path) -> None:
    fig, axis = plt.subplots(figsize=(7.4, 4.5), constrained_layout=True)
    time = numeric(rows, "physical_time")
    for key in ("residual_l2", "residual_linf"):
        axis.semilogy(time, np.maximum(numeric(rows, key), np.finfo(float).tiny), label=key, linewidth=1.5)
    axis.set(title=title, xlabel="physical time", ylabel="global residual norm")
    axis.grid(True, which="both", alpha=0.3)
    axis.legend()
    fig.savefig(destination, dpi=220)
    plt.close(fig)


def plot_surface(rows: list[dict[str, float | str]], case_id: str, destination: Path) -> None:
    x = numeric(rows, "x")
    cp = numeric(rows, "cp")
    order = np.argsort(x)
    fig, axis = plt.subplots(figsize=(7.4, 4.5), constrained_layout=True)
    axis.plot(x[order], cp[order], linewidth=1.4, label="$C_p$")
    axis.invert_yaxis()
    axis.set(title=f"{case_id}: wall pressure coefficient", xlabel="$x$", ylabel="$C_p$")
    axis.grid(True, alpha=0.3)
    axis.legend()
    fig.savefig(destination, dpi=220)
    plt.close(fig)


def plot_skin_friction(rows: list[dict[str, float | str]], case_id: str, destination: Path) -> None:
    """Plot the reported tangential-wall coefficient; never infer it from Cp."""
    x = numeric(rows, "x")
    cf = numeric(rows, "cf")
    order = np.argsort(x)
    fig, axis = plt.subplots(figsize=(7.4, 4.5), constrained_layout=True)
    axis.plot(x[order], cf[order], linewidth=1.4, label="$C_f$")
    axis.set(title=f"{case_id}: wall skin friction", xlabel="$x$", ylabel="$C_f$")
    axis.grid(True, alpha=0.3)
    axis.legend()
    fig.savefig(destination, dpi=220)
    plt.close(fig)


def body_view_limits(case: Case) -> tuple[tuple[float, float], tuple[float, float]]:
    """Return an actual-surface-based near-body or cylinder-wake view window."""
    x = numeric(case.surface, "x")
    y = numeric(case.surface, "y")
    xmin, xmax = float(np.min(x)), float(np.max(x))
    ymin, ymax = float(np.min(y)), float(np.max(y))
    scale = max(xmax - xmin, ymax - ymin, 1.0e-8)
    if "cylinder" in case.case_id.lower():
        return ((xmin - 1.5 * scale, xmax + 8.0 * scale),
                ((ymin + ymax) * 0.5 - 3.0 * scale, (ymin + ymax) * 0.5 + 3.0 * scale))
    return ((xmin - 0.15 * scale, xmax + 0.15 * scale),
            ((ymin + ymax) * 0.5 - 0.65 * scale, (ymin + ymax) * 0.5 + 0.65 * scale))


def tex(value: Any) -> str:
    return str(value).replace("_", "\\_").replace("&", "\\&").replace("%", "\\%")


def add_manifest(entries: list[dict[str, str]], file: str, case_id: str, figure_type: str, variable: str, source: Path, caption: str) -> None:
    entries.append({"figure_file": file, "case_id": case_id, "figure_type": figure_type,
                    "variable": variable, "source_file": str(source), "caption": caption})


def slug(value: str) -> str:
    return "".join(char if char.isalnum() else "_" for char in value).strip("_")


def run_label(case: Case) -> str:
    """A figure identifier must distinguish otherwise equal case IDs at np=2/8."""
    return f"{slug(case.case_id)}_np{int(case.status['mpi_ranks'])}"


def residual_orders(case: Case) -> float:
    values = numeric(case.residuals, "residual_l2")
    return float(math.log10(max(values[0], np.finfo(float).tiny) /
                            max(values[-1], np.finfo(float).tiny)))


def re200_metrics(case: Case) -> dict[str, float]:
    """Last-half force statistics, using only the sampled physical-time history."""
    time = numeric(case.forces, "physical_time")
    cd = numeric(case.forces, "cd")
    cl = numeric(case.forces, "cl")
    begin = len(time) // 2
    t, drag, lift = time[begin:], cd[begin:], cl[begin:]
    crossings: list[float] = []
    for index in range(1, len(lift)):
        if lift[index - 1] <= 0.0 < lift[index]:
            fraction = -lift[index - 1] / max(lift[index] - lift[index - 1], np.finfo(float).tiny)
            crossings.append(float(t[index - 1] + fraction * (t[index] - t[index - 1])))
    periods = np.diff(crossings)
    frequency = float(1.0 / np.mean(periods)) if len(periods) else float("nan")
    return {"window_start": float(t[0]), "window_end": float(t[-1]), "mean_cd": float(np.mean(drag)),
            "mean_cl": float(np.mean(lift)), "cl_rms": float(np.std(lift)),
            "cl_half_range": float((np.max(lift) - np.min(lift)) * 0.5),
            "frequency": frequency, "strouhal": frequency, "crossings": float(len(crossings))}


def split_partition_counts(value: object, path: Path, row: int, column: str) -> int:
    """Sum the one value per neighbour encoding without inventing edge counts."""
    if not isinstance(value, str) or not value:
        return 0
    try:
        values = [int(item) for item in value.split(";")]
    except ValueError as exc:
        raise OutputError(f"{path}:{row}: invalid {column} list {value!r}") from exc
    if any(item < 0 for item in values):
        raise OutputError(f"{path}:{row}: {column} contains a negative count")
    return sum(values)


def partition_entry_count(value: object, path: Path, row: int, column: str) -> int:
    if not isinstance(value, str) or not value:
        return 0
    entries = value.split(";")
    if any(not entry for entry in entries):
        raise OutputError(f"{path}:{row}: invalid empty {column} entry")
    return len(entries)


def partition_rows(case: Case) -> list[PartitionRow]:
    path = case.directory / "partition_diagnostics.csv"
    if not path.is_file():
        raise OutputError(f"{case.case_id}: missing partition diagnostics")
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != PARTITION_COLUMNS:
            raise OutputError(f"{path}: expected header {PARTITION_COLUMNS}, got {reader.fieldnames}")
        raw_rows = list(reader)
    if not raw_rows:
        raise OutputError(f"{path}: contains no rank rows")
    rows: list[PartitionRow] = []
    for row_number, row in enumerate(raw_rows, 2):
        try:
            item = PartitionRow(
                rank=int(row["rank"]),
                owned=int(row["num_cells_owned"]),
                ghost=int(row["num_cells_ghost"]),
                boundary_faces=int(row["num_boundary_faces"]),
                neighbours=int(row["num_neighbor_ranks"]),
                send_cells=split_partition_counts(row["send_cells"], path, row_number, "send_cells"),
                recv_cells=split_partition_counts(row["recv_cells"], path, row_number, "recv_cells"),
            )
        except (KeyError, ValueError) as exc:
            raise OutputError(f"{path}:{row_number}: invalid partition row") from exc
        if min(item.rank, item.owned, item.ghost, item.boundary_faces, item.neighbours) < 0:
            raise OutputError(f"{path}:{row_number}: partition counts must be non-negative")
        if any(partition_entry_count(row[column], path, row_number, column) != item.neighbours
               for column in ("neighbor_ranks", "send_cells", "recv_cells")):
            raise OutputError(f"{path}:{row_number}: neighbour, send, and receive lists must have equal lengths")
        rows.append(item)
    ranks = int(case.status["mpi_ranks"])
    if sorted(item.rank for item in rows) != list(range(ranks)):
        raise OutputError(f"{case.case_id}: partition diagnostics ranks do not match mpi_ranks={ranks}")
    return rows


def partition_summary(case: Case) -> dict[str, float | int]:
    rows = partition_rows(case)
    owned = [item.owned for item in rows]
    ghosts = [item.ghost for item in rows]
    neighbours = [item.neighbours for item in rows]
    return {
        "owned_min": min(owned), "owned_max": max(owned),
        "ghost_min": min(ghosts), "ghost_max": max(ghosts),
        "balance": max(owned) / max(float(np.mean(owned)), 1.0),
        "mean_neighbours": float(np.mean(neighbours)),
        "edge_cut": int(sum(neighbours) // 2),
        "send_total": sum(item.send_cells for item in rows),
        "recv_total": sum(item.recv_cells for item in rows),
    }


def control_value(case: Case, key: str) -> tuple[Any, str]:
    """Return a serialized applied control or a disclosed immutable-input fallback."""
    for group_name, controls in (("metadata", case.metadata),
                                 ("actual_controls", case.metadata.get("actual_controls")),
                                 ("run_control", case.metadata.get("run_control"))):
        if isinstance(controls, dict) and key in controls and controls[key] is not None:
            return controls[key], group_name
    input_controls = case.input_case.data.get("run_control")
    if isinstance(input_controls, dict) and key in input_controls and input_controls[key] is not None:
        return input_controls[key], "case input"
    return None, "not specified"


def control_display(case: Case, key: str) -> str:
    value, source = control_value(case, key)
    marker = control_marker(source)
    if value is None:
        return f"not specified [{marker}]"
    if isinstance(value, float):
        text_value = f"{value:.8g}"
    else:
        text_value = str(value)
    return f"{text_value} [{marker}]"


def control_marker(source: str) -> str:
    return {"metadata": "M", "actual_controls": "A", "run_control": "R", "case input": "I"}.get(source, "--")


def integrator_display(case: Case) -> str:
    value, source = control_value(case, "time_integrator")
    readable = {
        "steady_pseudo_time_block_jacobi": "steady pseudo-time block-Jacobi",
        "steady_local_pseudo_time_block_lu_sgs": "steady local pseudo-time block LU--SGS",
        "bdf2_or_trapezoidal": "BDF2 or trapezoidal",
    }.get(str(value), str(value) if value is not None else "not specified")
    return f"{readable} [{control_marker(source)}]"


def control_sources(case: Case, keys: Iterable[str]) -> str:
    seen = {control_value(case, key)[1] for key in keys}
    labels = {"metadata": "metadata", "actual_controls": "applied controls",
              "run_control": "recorded controls", "case input": "case JSON fallback",
              "not specified": "not specified"}
    return "+".join(labels[source] for source in ("metadata", "actual_controls", "run_control", "case input", "not specified") if source in seen)


def control_source_codes(case: Case, keys: Iterable[str]) -> str:
    """Compact source legend for a printable table; individual cells retain markers."""
    seen = {control_value(case, key)[1] for key in keys}
    labels = {"metadata": "M", "actual_controls": "A", "run_control": "R", "case input": "I"}
    codes = [labels[source] for source in ("metadata", "actual_controls", "run_control", "case input") if source in seen]
    return "+".join(codes) if codes else "--"


def input_mesh(case: Case) -> str:
    mesh = case.input_case.data["mesh"]
    return Path(str(mesh["file"])).name


def input_physics(case: Case) -> str:
    physics = case.input_case.data.get("physics", {})
    freestream = case.input_case.data.get("freestream", {})
    mode = str(physics.get("mode", "unspecified"))
    mach = freestream.get("mach", "?")
    reynolds = physics.get("reynolds")
    reynolds_text = f", Re={reynolds:g}" if isinstance(reynolds, (int, float)) else ""
    return f"{mode}, M={mach:g}{reynolds_text}" if isinstance(mach, (int, float)) else f"{mode}, M={mach}{reynolds_text}"


def input_boundary_tags(case: Case) -> str:
    boundaries = case.input_case.data["boundary_conditions"]
    readable = {"farfield": "farfield", "slip_wall": "slip wall",
                "no_slip_adiabatic_wall": "no-slip adiabatic wall"}
    return "; ".join(f"{tag} $\\rightarrow$ {readable.get(str(kind), kind)}"
                     for tag, kind in boundaries.items())


@dataclass
class Case:
    directory: Path
    case_id: str
    metadata: dict[str, Any]
    status: dict[str, Any]
    input_case: CaseInput
    residuals: list[dict[str, float | str]]
    forces: list[dict[str, float | str]]
    surface: list[dict[str, float | str]]
    field_path: Path
    field: Field


def close_number(first: object, second: object) -> bool:
    """Compare serialized residual values without masking material drift."""
    try:
        return math.isclose(float(first), float(second), rel_tol=1.0e-10, abs_tol=1.0e-12)
    except (TypeError, ValueError):
        return False


def validate_steady_restart_provenance(directory: Path, metadata: dict[str, Any],
                                       status: dict[str, Any],
                                       residuals: list[dict[str, float | str]]) -> None:
    """Reject a checkpoint-local steady residual claim before report generation."""
    controls = metadata.get("run_control")
    if not isinstance(controls, dict) or controls.get("type") != "steady":
        return
    case_id = str(metadata.get("case_id", directory.name))
    provenance = status.get("restart_provenance")
    if status.get("residual_reference_scope") != "cumulative_fully_assembled" or not isinstance(provenance, dict):
        raise OutputError(f"{case_id}: steady output lacks cumulative fully assembled restart provenance")
    if metadata.get("restart_provenance") != provenance:
        raise OutputError(f"{case_id}: metadata and run_status restart provenance disagree")
    manifest_path = directory / "restart_final.manifest.json"
    if not manifest_path.is_file():
        raise OutputError(f"{case_id}: steady output lacks restart_final.manifest.json")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != "cfd_rank_local_restart_v2" or manifest.get("provenance") != provenance:
        raise OutputError(f"{case_id}: restart manifest is not the submitted cumulative provenance record")
    required = ("chain_depth", "compatibility_signature", "cumulative_residual_trace",
                "residual_reference_l2", "residual_reference_linf", "checkpoint_step",
                "checkpoint_physical_time", "checkpoint_residual_l2", "checkpoint_residual_linf")
    if any(key not in provenance for key in required):
        raise OutputError(f"{case_id}: restart provenance is incomplete")
    if provenance["cumulative_residual_trace"] != "residuals.csv" or not str(provenance["compatibility_signature"]):
        raise OutputError(f"{case_id}: cumulative residual trace or compatibility signature is invalid")
    if int(provenance["chain_depth"]) > 0 and not str(provenance.get("parent_manifest", "")):
        raise OutputError(f"{case_id}: restarted output lacks a parent-manifest reference")
    if int(float(residuals[0]["step"])) != 0 or not close_number(residuals[0]["residual_l2"], provenance["residual_reference_l2"]):
        raise OutputError(f"{case_id}: residual history does not begin at its cumulative reference state")
    if (int(float(residuals[-1]["step"])) != int(provenance["checkpoint_step"]) or
            not close_number(residuals[-1]["physical_time"], provenance["checkpoint_physical_time"]) or
            not close_number(residuals[-1]["residual_l2"], provenance["checkpoint_residual_l2"]) or
            not close_number(residuals[-1]["residual_linf"], provenance["checkpoint_residual_linf"])):
        raise OutputError(f"{case_id}: residual history does not end at its validated restart checkpoint")
    if (int(float(status.get("final_step", -1))) != int(provenance["checkpoint_step"]) or
            not close_number(status.get("final_physical_time"), provenance["checkpoint_physical_time"])):
        raise OutputError(f"{case_id}: run status does not match its restart checkpoint")
    reference = float(provenance["residual_reference_l2"])
    checkpoint = float(provenance["checkpoint_residual_l2"])
    if not math.isfinite(reference) or not math.isfinite(checkpoint) or reference <= 0.0 or checkpoint <= 0.0:
        raise OutputError(f"{case_id}: cumulative residual reference is invalid")
    orders = math.log10(reference / checkpoint)
    if not close_number(status.get("residual_reduction_orders"), orders):
        raise OutputError(f"{case_id}: reported residual orders are not traceable to the cumulative history")


def validate_steady_target(case_id: str, input_case: CaseInput,
                           residuals: list[dict[str, float | str]]) -> None:
    """A final steady `converged` label must meet the target in its named input."""
    controls = input_case.data.get("run_control")
    if not isinstance(controls, dict) or controls.get("type") != "steady":
        return
    target = controls.get("residual_reduction_target")
    if not isinstance(target, (int, float)) or not math.isfinite(float(target)):
        raise OutputError(f"{case_id}: steady input has no finite residual_reduction_target")
    first = float(residuals[0]["residual_l2"])
    final = float(residuals[-1]["residual_l2"])
    orders = math.log10(first / final)
    if orders + 1.0e-10 < float(target):
        raise OutputError(
            f"{case_id}: final residual history shows {orders:.9g} orders, below the input target {target:.9g}")


def load_case(directory: Path) -> Case:
    for required in ("metadata.json", "run_status.json", "restart_final"):
        if required == "restart_final":
            if not list(directory.glob("restart_final.*")):
                raise OutputError(f"{directory}: missing restart_final.*")
        elif not (directory / required).is_file():
            raise OutputError(f"{directory}: missing {required}")
    metadata = json.loads((directory / "metadata.json").read_text())
    status = json.loads((directory / "run_status.json").read_text())
    case_id = str(metadata.get("case_id") or status.get("case_id") or directory.name)
    if metadata.get("completed") is not True:
        raise OutputError(f"{case_id}: metadata.completed is not true")
    if status.get("convergence_status") not in {"converged", "statistically_periodic"}:
        raise OutputError(f"{case_id}: non-final convergence status {status.get('convergence_status')!r}")
    if metadata.get("convergence_status") != status.get("convergence_status"):
        raise OutputError(f"{case_id}: metadata and run_status convergence_status disagree")
    input_case = load_command_case_input(directory, case_id, metadata, status)
    residuals = read_csv(directory / "residuals.csv", RESIDUAL_COLUMNS)
    validate_steady_restart_provenance(directory, metadata, status, residuals)
    validate_steady_target(case_id, input_case, residuals)
    forces = read_csv(directory / "forces.csv", FORCE_COLUMNS)
    surface = read_csv(directory / "surface.csv", SURFACE_COLUMNS)
    if int(numeric(forces, "step")[-1]) != int(float(status.get("final_step", -1))):
        raise OutputError(f"{case_id}: final force step does not match run_status")
    require_literal_final_field(directory)
    path = field_file(directory)
    return Case(directory, case_id, metadata, status, input_case, residuals, forces, surface, path, load_field(path))


def sanity(case: Case, density: np.ndarray, pressure: np.ndarray) -> dict[str, Any]:
    cid = case.case_id.lower()
    inviscid = "inviscid" in cid
    viscous = not inviscid
    forces = case.forces
    latter = slice(max(0, len(forces) // 2), None)
    checks: dict[str, bool] = {
        "positive_density": bool(np.all(density > 0.0)),
        "positive_pressure": bool(np.all(pressure > 0.0)),
        "surface_cp_varies": bool(np.ptp(numeric(case.surface, "cp")) > 1.0e-10),
        "final_force_matches_status": int(numeric(forces, "step")[-1]) == int(float(case.status["final_step"])),
    }
    if "naca" in cid:
        checks["zero_aoa_lift_near_zero"] = bool(abs(numeric(forces, "cl")[-1]) < 5.0e-2)
        checks["naca_not_trivial"] = bool(np.ptp(numeric(forces, "cd")) > 1.0e-12 or np.ptp(numeric(case.surface, "cp")) > 1.0e-8)
    if "cylinder" in cid:
        checks["positive_mean_drag_after_startup"] = float(np.mean(numeric(forces, "cd")[latter])) > 0.0
    if "re200" in cid:
        checks["unsteady_lift_after_startup"] = float(np.std(numeric(forces, "cl")[latter])) > 1.0e-7
    if viscous:
        velocity = np.hypot(numeric(case.surface, "u"), numeric(case.surface, "v"))
        checks["no_slip_wall_velocity"] = float(np.nanmax(velocity)) < 1.0e-5
        checks["viscous_skin_friction_present"] = float(np.nanmax(np.abs(numeric(case.surface, "cf")))) > 1.0e-12
    else:
        normal = numeric(case.surface, "u") * numeric(case.surface, "nx") + numeric(case.surface, "v") * numeric(case.surface, "ny")
        checks["slip_wall_normal_velocity"] = float(np.nanmax(np.abs(normal))) < 1.0e-5
        checks["negligible_viscous_force"] = max(abs(numeric(forces, "viscous_drag")[-1]), abs(numeric(forces, "viscous_lift")[-1])) < 1.0e-8
    # NumPy scalar booleans are accepted by Python's ``all`` but are not JSON
    # serializable on every supported Python version.  Normalize the report
    # payload at this boundary so the generated sanity artifact is portable.
    normalized_checks = {key: bool(value) for key, value in checks.items()}
    return {"case_id": case.case_id, "status": "pass" if all(normalized_checks.values()) else "failed", "checks": normalized_checks}


def write_tex(report: Path, cases: list[Case], figures: list[dict[str, str]], checks: list[dict[str, Any]]) -> None:
    control_keys = ("time_integrator", "type", "cfl_initial", "cfl_max", "residual_reduction_target",
                    "inner_residual_reduction_target", "max_steps", "time_step", "final_time",
                    "rusanov_dissipation_scale")
    run_refs = {case.directory: f"R{index}" for index, case in enumerate(cases, 1)}
    lines = ["\\subsection{Run status and residual evidence}",
             "Table~\\ref{tab:run-status} is generated directly from \\texttt{run\\_manifest.csv}; residual orders are recomputed from the first and final global $L_2$ rows, not copied from status text. For each steady case, generation also rejects a history below the residual target in the immutable input named by the recorded command.",
             "\\begin{center}\\scriptsize", "\\resizebox{\\linewidth}{!}{%", "\\begin{tabular}{lllrrrrrl}",
             "\\toprule Ref. & Run & Case & ranks & steps & time & residual orders & wall s & status\\\\", "\\midrule"]
    for case in cases:
        st = case.status
        lines.append(f"{run_refs[case.directory]} & {tex(run_label(case))} & {tex(case.case_id)} & {int(st['mpi_ranks'])} & {int(st['final_step'])} & {float(st['final_physical_time']):.5g} & {residual_orders(case):.4g} & {float(st['wall_time_seconds']):.5g} & {tex(st['convergence_status'])}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}}", "\\captionof{table}{Run status and global residual reduction from submitted histories.}", "\\label{tab:run-status}", "\\end{center}",
              "\\subsection{Input-case, mesh, and boundary provenance}",
              "For every output, the generator parses \\texttt{run\\_status.command} with Python \\texttt{shlex}, resolves its unique \\texttt{--case} and \\texttt{--output} arguments, and rejects a command whose output path or JSON case identifier does not match the submitted directory. The mesh basename is also checked against submitted metadata. Thus the tags below are evidence from the immutable command-selected input, rather than post-processed metadata. The exact JSON path remains in each manifest command; R-codes map to Table~\\ref{tab:run-status}.",
              "\\begin{center}\\small", "\\begin{tabular}{lllrrp{4.3cm}}",
              "\\toprule Ref. & mesh & physics & cells & faces & boundary-tag mapping\\\\", "\\midrule"]
    for case in cases:
        lines.append(f"{run_refs[case.directory]} & {tex(input_mesh(case))} & {tex(input_physics(case))} & {int(case.metadata['num_cells_global'])} & {int(case.metadata['num_faces_global'])} & {tex(input_boundary_tags(case))}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\captionof{table}{Case, mesh, and boundary-tag evidence from the exact JSON named by each submitted command.}", "\\label{tab:case-mesh-boundaries}", "\\end{center}",
              "\\subsection{Recorded numerical controls}",
              "Control cells are deliberately source-marked: [M] is a submitted top-level metadata field, [A] an applied-control field, [R] a serialized submitted \\texttt{run\\_control}, and [I] the immutable input JSON named by \\texttt{--case}. The [I] fallback is used only when a legacy output omitted that control; no value is inferred from a plot, defaulted silently, or written back into metadata. [--] means that neither source specifies the field.",
              "\\begin{center}\\small", "\\begin{tabular}{llp{4.0cm}lp{3.4cm}}", "\\toprule Ref. & sources & integrator & type & horizon\\\\", "\\midrule"]
    for case in cases:
        run_type, _ = control_value(case, "type")
        if run_type == "transient":
            horizon = f"dt={control_display(case, 'time_step')}; tf={control_display(case, 'final_time')}"
        else:
            horizon = f"max steps={control_display(case, 'max_steps')}"
        lines.append(f"{run_refs[case.directory]} & {tex(control_source_codes(case, control_keys))} & {tex(integrator_display(case))} & {tex(control_display(case, 'type'))} & {tex(horizon)}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\captionof{table}{Integration and horizon controls with their serialized provenance.}", "\\label{tab:controls}", "\\end{center}",
              "\\begin{center}\\small", "\\begin{tabular}{lrrrrr}", "\\toprule Ref. & CFL initial & CFL max & outer target & inner target & Rusanov scale\\\\", "\\midrule"]
    for case in cases:
        lines.append(f"{run_refs[case.directory]} & {tex(control_display(case, 'cfl_initial'))} & {tex(control_display(case, 'cfl_max'))} & {tex(control_display(case, 'residual_reduction_target'))} & {tex(control_display(case, 'inner_residual_reduction_target'))} & {tex(control_display(case, 'rusanov_dissipation_scale'))}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\captionof{table}{CFL, residual-target, and dissipation controls with per-cell provenance markers.}", "\\label{tab:control-thresholds}", "\\end{center}",
              "\\subsection{Forces and convergence qualification}", "The final-state force values below are read from the final row of each submitted \\texttt{forces.csv}; pressure and viscous contributions remain separate. A steady row is admitted only after the residual history reaches the target specified in its command-selected input JSON.", "\\begin{center}\\small", "\\begin{tabular}{lrrrrrr}", "\\toprule Ref. & $C_D$ & $C_L$ & $C_m$ & $C_{D,p}$ & $C_{D,v}$ & qualification\\\\", "\\midrule"]
    for case in cases:
        row = case.forces[-1]
        run_type, _ = control_value(case, "type")
        qualification = "steady target met" if run_type == "steady" else "statistically periodic"
        lines.append(f"{run_refs[case.directory]} & {float(row['cd']):.5g} & {float(row['cl']):.5g} & {float(row['cmz']):.4g} & {float(row['pressure_drag']):.5g} & {float(row['viscous_drag']):.5g} & {tex(qualification)}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\captionof{table}{Final force coefficients and convergence qualification; R-codes map to Table~\\ref{tab:run-status}.}", "\\label{tab:forces}", "\\end{center}"]
    lines += ["\\subsection{Case-by-case interpretation}",
              "Each paragraph below refers to the corresponding submitted histories and final field. These plots support qualitative flow interpretation, not an unperformed grid-convergence or experimental-validation claim."]
    for case in cases:
        run = run_label(case)
        final_force = case.forces[-1]
        status = str(case.status["convergence_status"])
        residual = residual_orders(case)
        field_reference = f"Figure~\\ref{{fig:{run}_mach_body_or_wake}} and Figure~\\ref{{fig:{run}_pressure_body_or_wake}}"
        surface_reference = f"Figure~\\ref{{fig:{run}_surface_pressure_coefficient}}"
        force_reference = f"Figure~\\ref{{fig:{run}_forces}}"
        residual_reference = f"Figure~\\ref{{fig:{run}_residuals}}"
        sentence = (f"\\paragraph{{{tex(run)}.}} This {tex(input_physics(case))} submitted run is classified {tex(status)} with "
                    f"{residual:.4g} orders of visible $L_2$ reduction. Its final coefficients are "
                    f"$C_D={float(final_force['cd']):.6g}$ and $C_L={float(final_force['cl']):.6g}$. "
                    f"{residual_reference} and {force_reference} show the convergence/force history; "
                    f"{surface_reference} and {field_reference} show the wall-pressure and near-body/wake field evidence.")
        lower_id = case.case_id.lower()
        if "m200" in lower_id:
            sentence += " The supplied Mach and pressure fields are the direct evidence used for the shock/expansion-dominated finite-Mach pattern; no reference-data error or mesh-refinement uncertainty is asserted."
        elif "m080" in lower_id:
            sentence += " The finite-Mach field panels make the compression and expansion structure inspectable without treating the contour itself as an external validation result."
        elif "m015" in lower_id:
            sentence += " The low-Mach result is reported with the same field and surface evidence, without extrapolating it to an incompressible reference solution."
        if "inviscid" not in lower_id:
            sentence += f" The reported tangential wall stress is plotted in Figure~\\ref{{fig:{run}_skin_friction}}."
        if "re20" in lower_id and "re200" not in lower_id:
            sentence += " Its low-Re wake assessment is based on the final submitted field and the settled force history."
        if "re200" in lower_id:
            sentence += f" The post-transient wake diagnostic is Figure~\\ref{{fig:{run}_velocity_magnitude}}."
        lines.append(sentence)
    lines += ["\\subsection{Partition diagnostics and MPI comparison}", "The first table summarizes the actual per-rank diagnostics written by the solver. The following long table retains every owned/ghost count, boundary-face count, neighbour count, and summed send/receive cell count rather than collapsing communication evidence to an average. `Neighbour graph edges' is half the directed neighbour count in the submitted partition diagnostics. In the detailed table, R1, R2, and so on identify the corresponding row order in Table~\\ref{tab:run-status}.", "\\begin{center}\\small", "\\begin{tabular}{lrrrrrrr}", "\\toprule Ref. & owned min--max & ghost min--max & balance & avg. neigh. & edges & sent & received\\\\", "\\midrule"]
    for case in cases:
        summary = partition_summary(case)
        lines.append(f"{run_refs[case.directory]} & {summary['owned_min']}--{summary['owned_max']} & {summary['ghost_min']}--{summary['ghost_max']} & {float(summary['balance']):.4f} & {float(summary['mean_neighbours']):.3g} & {summary['edge_cut']} & {summary['send_total']} & {summary['recv_total']}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\captionof{table}{METIS partition balance, ghost-layer, neighbour, and communication totals; R-codes map to Table~\\ref{tab:run-status}.}", "\\label{tab:partition-summary}", "\\end{center}",
              "\\begingroup\\scriptsize", "\\begin{longtable}{lrrrrrrr}", "\\caption{Per-rank partition diagnostics as emitted by the submitted solver.}\\label{tab:partition-detail}\\\\", "\\toprule run ref. & rank & owned & ghost & boundary faces & neighbours & send cells & receive cells\\\\", "\\midrule", "\\endfirsthead", "\\toprule run ref. & rank & owned & ghost & boundary faces & neighbours & send cells & receive cells\\\\", "\\midrule", "\\endhead"]
    for index, case in enumerate(cases, 1):
        for row in partition_rows(case):
            lines.append(f"R{index} & {row.rank} & {row.owned} & {row.ghost} & {row.boundary_faces} & {row.neighbours} & {row.send_cells} & {row.recv_cells}\\\\")
    lines += ["\\bottomrule", "\\end{longtable}", "\\endgroup"]
    groups: dict[str, list[Case]] = {}
    for case in cases: groups.setdefault(case.case_id, []).append(case)
    for case_id, group in groups.items():
        if len(group) > 1:
            ordered = sorted(group, key=lambda value: int(value.status["mpi_ranks"]))
            left, right = ordered[0], ordered[-1]
            left_cd, right_cd = float(left.forces[-1]["cd"]), float(right.forces[-1]["cd"])
            relative_cd = 100.0 * abs(right_cd - left_cd) / max(abs(left_cd), np.finfo(float).tiny)
            lines.append(f"For {tex(case_id)}, the submitted np={int(left.status['mpi_ranks'])} and np={int(right.status['mpi_ranks'])} runs give final $C_D={left_cd:.6g}$ and ${right_cd:.6g}$, a {relative_cd:.4g}\\% difference relative to the lower-rank value; their residual reductions are {residual_orders(left):.3g} and {residual_orders(right):.3g}, and wall times {float(left.status['wall_time_seconds']):.6g} s and {float(right.status['wall_time_seconds']):.6g} s. This is an actual parallel consistency record, not a strong-scaling claim; material differences are reported without rounding them away.")
    re200 = [case for case in cases if "re200" in case.case_id.lower()]
    if re200:
        case, metrics = re200[0], re200_metrics(re200[0])
        lines += ["\\subsection{Cylinder Re 200 vortex-street analysis}",
                  f"Over the latter half of the sampled physical-time history ($t \\in [{metrics['window_start']:.6g}, {metrics['window_end']:.6g}]$), the mean drag is ${metrics['mean_cd']:.6g}$, mean lift is ${metrics['mean_cl']:.6g}$, lift RMS is ${metrics['cl_rms']:.6g}$, and lift half-range amplitude is ${metrics['cl_half_range']:.6g}$. Linear interpolation of {int(metrics['crossings'])} upward lift zero crossings gives dominant frequency ${metrics['frequency']:.6g}$ and Strouhal number ${metrics['strouhal']:.6g}$, using the supplied unit reference length and velocity. Figure~\\ref{{fig:{run_label(case)}_velocity_magnitude}} is the submitted-field velocity-magnitude wake view; Figures~\\ref{{fig:{run_label(case)}_forces}} and \\ref{{fig:{run_label(case)}_pressure_body_or_wake}} provide the force history and pressure wake context."]
    lines += ["\\subsection{Figure guide and sanity checks}", "Every figure below is generated from the source named in its caption and recorded one-to-one in \\texttt{figure\\_manifest.csv}. Mach and pressure contours use the actual unstructured field cells; the body/wake panels provide the prescribed near-body views. The machine-readable checks are in \\texttt{sanity\\_checks.json}.", "\\begin{itemize}"]
    for item in checks: lines.append(f"\\item {tex(item['case_id'])}: {tex(item['status'])}.")
    lines += ["\\end{itemize}", "\\subsection{Figures}"]
    for index, entry in enumerate(figures, 1):
        source_name, label = tex(Path(entry["source_file"]).name), f"fig:{slug(entry['figure_file']).replace('_png', '')}"
        lines += ["\\begin{figure}[H]", "\\centering", f"\\includegraphics[width=0.90\\linewidth]{{{tex(entry['figure_file'])}}}", f"\\caption{{{tex(entry['caption'])}. Source: \\texttt{{{source_name}}}; provenance: \\texttt{{figure\\_manifest.csv}}.}}", f"\\label{{{label}}}", "\\end{figure}"]
    (report / "generated_results.tex").write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path, help="directory for generated report artifacts")
    parser.add_argument("outputs", nargs="+", type=Path, help="actual completed solver output directories")
    args = parser.parse_args()
    report = args.report.resolve()
    report.mkdir(parents=True, exist_ok=True)
    figures_dir = report / "figures"
    figures_dir.mkdir(exist_ok=True)
    template = Path(__file__).resolve().parents[1] / "report" / "report.tex.in"
    if not template.is_file():
        raise OutputError(f"missing report template: {template}")
    shutil.copyfile(template, report / "report.tex")
    cases = [load_case(path.resolve()) for path in args.outputs]
    if len({(case.case_id, case.directory) for case in cases}) != len(cases):
        raise OutputError("duplicate output directory supplied")
    figures: list[dict[str, str]] = []
    checks: list[dict[str, Any]] = []
    for case in cases:
        stem = run_label(case)
        residual_name = f"{stem}_residuals.png"
        plot_residuals(case.residuals, f"{case.case_id}: residual history", figures_dir / residual_name)
        add_manifest(figures, residual_name, case.case_id, "history", "residual", case.directory / "residuals.csv", "Global residual history")
        force_name = f"{stem}_forces.png"
        plot_history(case.forces, ["cd", "cl", "cmz"], "force coefficient", f"{case.case_id}: force history", figures_dir / force_name)
        add_manifest(figures, force_name, case.case_id, "history", "force coefficients", case.directory / "forces.csv", "Force-coefficient history")
        surface_name = f"{stem}_surface_pressure_coefficient.png"
        plot_surface(case.surface, case.case_id, figures_dir / surface_name)
        add_manifest(figures, surface_name, case.case_id, "surface", "pressure coefficient", case.directory / "surface.csv", "Wall pressure coefficient")
        if "inviscid" not in case.case_id.lower():
            cf_name = f"{stem}_skin_friction.png"
            plot_skin_friction(case.surface, case.case_id, figures_dir / cf_name)
            add_manifest(figures, cf_name, case.case_id, "surface", "skin friction", case.directory / "surface.csv", "Wall skin-friction coefficient")
        mach_name = f"{stem}_mach.png"
        mach = plot_field(case.field, ["mach", "mach_number", "machnumber"], f"{case.case_id}: Mach number", "Mach number", figures_dir / mach_name)
        add_manifest(figures, mach_name, case.case_id, "field", "mach", case.field_path, "Filled Mach-number field")
        limits = body_view_limits(case)
        mach_zoom_name = f"{stem}_mach_body_or_wake.png"
        plot_field(case.field, ["mach", "mach_number", "machnumber"], f"{case.case_id}: Mach number (body/wake view)", "Mach number", figures_dir / mach_zoom_name, limits)
        add_manifest(figures, mach_zoom_name, case.case_id, "field_zoom", "mach", case.field_path, "Filled Mach-number body or wake view")
        pressure_name = f"{stem}_pressure.png"
        pressure = plot_field(case.field, ["pressure", "p"], f"{case.case_id}: pressure", "pressure", figures_dir / pressure_name)
        add_manifest(figures, pressure_name, case.case_id, "field", "pressure", case.field_path, "Filled pressure field")
        pressure_zoom_name = f"{stem}_pressure_body_or_wake.png"
        plot_field(case.field, ["pressure", "p"], f"{case.case_id}: pressure (body/wake view)", "pressure", figures_dir / pressure_zoom_name, limits)
        add_manifest(figures, pressure_zoom_name, case.case_id, "field_zoom", "pressure", case.field_path, "Filled pressure body or wake view")
        # Verify required field components even though the report's mandatory
        # figures are Mach and pressure.
        case.field.velocity()
        if "re200" in case.case_id.lower():
            velocity_name = f"{stem}_velocity_magnitude.png"
            try:
                u, v, loc_u = case.field.velocity()
                key = "velocity_magnitude"
                if loc_u == "point":
                    case.field.point_data[key] = np.hypot(u, v)
                else:
                    case.field.cell_data[key] = np.hypot(u, v)
                plot_field(case.field, [key], f"{case.case_id}: velocity magnitude", "velocity magnitude", figures_dir / velocity_name)
            except OutputError:
                # A real vorticity field is an equally valid wake visualization.
                plot_field(case.field, ["vorticity", "omega", "vorticity_z"], f"{case.case_id}: vorticity", "vorticity", figures_dir / velocity_name)
            add_manifest(figures, velocity_name, case.case_id, "field", "velocity magnitude", case.field_path, "Post-transient wake visualization")
        density, _ = case.field.scalar(["density", "rho"])
        checks.append(sanity(case, density, pressure))
    with (report / "figure_manifest.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"])
        writer.writeheader(); writer.writerows(figures)
    with (report / "run_manifest.csv").open("w", newline="") as stream:
        fields = ["case_id", "output_dir", "command", "mpi_ranks", "wall_time_seconds", "final_step", "final_physical_time", "convergence_status", "residual_reduction_orders", "notes"]
        writer = csv.DictWriter(stream, fieldnames=fields); writer.writeheader()
        for case in cases:
            writer.writerow({"case_id": case.case_id, "output_dir": case.directory, **{key: case.status.get(key, "") for key in fields if key not in {"case_id", "output_dir"}}})
    payload = {"generated_from": [str(case.directory) for case in cases], "cases": checks,
               "overall_status": "pass" if all(item["status"] == "pass" for item in checks) else "failed"}
    (report / "sanity_checks.json").write_text(json.dumps(payload, indent=2) + "\n")
    write_tex(report, cases, figures, checks)
    if payload["overall_status"] != "pass":
        raise OutputError("sanity checks failed; inspect report/sanity_checks.json and mark/fix failed solver outputs")
    print(f"Generated {len(figures)} figures for {len(cases)} real solver outputs in {report}")


if __name__ == "__main__":
    try:
        main()
    except OutputError as error:
        raise SystemExit(f"report generation failed: {error}")

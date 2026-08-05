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


class OutputError(RuntimeError):
    pass


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


def numeric(rows: list[dict[str, float | str]], key: str) -> np.ndarray:
    return np.asarray([float(row[key]) for row in rows], dtype=float)


def field_file(case_dir: Path) -> Path:
    choices = sorted(case_dir.glob("field_final.*"))
    if not choices:
        raise OutputError(f"{case_dir}: missing field_final.*")
    supported = [item for item in choices if item.suffix.lower() in {".vtu", ".pvtu", ".vtk", ".cgns"}]
    if not supported:
        raise OutputError(f"{case_dir}: field file must be .vtu, .pvtu, .vtk, or .cgns; found {choices}")
    return supported[0]


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


@dataclass
class Case:
    directory: Path
    case_id: str
    metadata: dict[str, Any]
    status: dict[str, Any]
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
    residuals = read_csv(directory / "residuals.csv", RESIDUAL_COLUMNS)
    validate_steady_restart_provenance(directory, metadata, status, residuals)
    forces = read_csv(directory / "forces.csv", FORCE_COLUMNS)
    surface = read_csv(directory / "surface.csv", SURFACE_COLUMNS)
    if int(numeric(forces, "step")[-1]) != int(float(status.get("final_step", -1))):
        raise OutputError(f"{case_id}: final force step does not match run_status")
    path = field_file(directory)
    return Case(directory, case_id, metadata, status, residuals, forces, surface, path, load_field(path))


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
    lines = ["\\subsection{Run status}", "\\begin{center}", "\\begin{tabular}{lrrrrl}",
             "\\toprule Case & ranks & steps & physical time & wall seconds & status\\\\", "\\midrule"]
    for case in cases:
        st = case.status
        lines.append(f"{tex(case.case_id)} & {st.get('mpi_ranks','')} & {st.get('final_step','')} & {float(st.get('final_physical_time', 0)):.6g} & {float(st.get('wall_time_seconds', 0)):.6g} & {tex(st.get('convergence_status',''))}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\end{center}", "\\subsection{Final force coefficients}", "\\begin{center}", "\\begin{tabular}{lrrrrr}", "\\toprule Case & $C_D$ & $C_L$ & $C_m$ & pressure drag & viscous drag\\\\", "\\midrule"]
    for case in cases:
        row = case.forces[-1]
        lines.append(f"{tex(case.case_id)} & {float(row['cd']):.6g} & {float(row['cl']):.6g} & {float(row['cmz']):.6g} & {float(row['pressure_drag']):.6g} & {float(row['viscous_drag']):.6g}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\end{center}", "\\subsection{Physics sanity checks}", "\\begin{itemize}"]
    for item in checks:
        lines.append(f"\\item {tex(item['case_id'])}: {tex(item['status'])} (see \\texttt{{sanity\\_checks.json}}).")
    lines.append("\\end{itemize}")
    qualifications = []
    for case in cases:
        note = str(case.status.get("notes", "")).strip()
        if "plateau" in note.lower() or "target was not attained" in note.lower():
            qualifications.append((case.case_id, note))
    if qualifications:
        lines += ["\\subsection{Convergence qualifications}", "\\begin{itemize}"]
        for case_id, note in qualifications:
            lines.append(f"\\item \\texttt{{{tex(case_id)}}}: {tex(note)}.")
        lines.append("\\end{itemize}")
    rank_groups: dict[str, set[int]] = {}
    for case in cases:
        rank_groups.setdefault(case.case_id, set()).add(int(case.status.get("mpi_ranks", 0)))
    lines += ["\\subsection{MPI rank-count evidence}", "The following table is derived only from supplied runs; missing comparisons are intentionally reported as missing.", "\\begin{center}", "\\begin{tabular}{ll}", "\\toprule Case & observed MPI ranks\\\\", "\\midrule"]
    for case_id, ranks in rank_groups.items():
        lines.append(f"{tex(case_id)} & {', '.join(map(str, sorted(ranks)))}\\\\")
    lines += ["\\bottomrule", "\\end{tabular}", "\\end{center}", "\\subsection{Figures}"]
    for index, entry in enumerate(figures, 1):
        source_name = tex(Path(entry["source_file"]).name)
        lines += ["\\begin{figure}[H]", "\\centering", f"\\includegraphics[width=0.90\\linewidth]{{{tex(entry['figure_file'])}}}", f"\\caption{{{tex(entry['caption'])}. Source: \\texttt{{{source_name}}} (see \\texttt{{figure\\_manifest.csv}}).}}", f"\\label{{fig:auto-{index}}}", "\\end{figure}"]
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
        stem = "".join(char if char.isalnum() else "_" for char in case.case_id).strip("_")
        residual_name = f"{stem}_residuals.png"
        plot_residuals(case.residuals, f"{case.case_id}: residual history", figures_dir / residual_name)
        add_manifest(figures, residual_name, case.case_id, "history", "residual", case.directory / "residuals.csv", "Global residual history")
        force_name = f"{stem}_forces.png"
        plot_history(case.forces, ["cd", "cl", "cmz"], "force coefficient", f"{case.case_id}: force history", figures_dir / force_name)
        add_manifest(figures, force_name, case.case_id, "history", "force coefficients", case.directory / "forces.csv", "Force-coefficient history")
        surface_name = f"{stem}_surface_pressure_coefficient.png"
        plot_surface(case.surface, case.case_id, figures_dir / surface_name)
        add_manifest(figures, surface_name, case.case_id, "surface", "pressure coefficient", case.directory / "surface.csv", "Wall pressure coefficient")
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

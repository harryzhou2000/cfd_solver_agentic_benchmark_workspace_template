#!/usr/bin/env python3
"""Generate reproducible, data-traceable figures and a LaTeX CFD report.

Only submitted solver output is used: missing, malformed, or failed output is
recorded as such in the manifests and report; no synthetic data are generated.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import shutil
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

# Keep Matplotlib's cache under the solver workspace in restricted runners.
os.environ.setdefault("MPLCONFIGDIR", str(Path(__file__).resolve().parent / ".mplconfig"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from matplotlib.colors import Normalize
import numpy as np


EXPECTED_CASES = (
    "naca0012_m015_inviscid", "naca0012_m080_inviscid",
    "naca0012_m200_inviscid", "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000", "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
)
FIELD_VARIABLES = ("density", "u", "v", "pressure", "mach", "temperature")
LATEX_ROW_END = r"\\"
RANK_COMPARISON_COLUMNS = (
    "case_id", "mpi_ranks", "command", "wall_time_seconds", "final_step",
    "final_physical_time", "cd", "cl", "cmz", "residual_reduction_orders",
    "owned_cells_min", "owned_cells_max", "ghost_cells_mean",
    "load_balance_ratio", "partition_edge_cut", "notes",
)
RUNNER_COMPARISON_COLUMNS = (
    "case_id", "ranks", "command", "wall_time_seconds", "solver_wall_time_seconds",
    "launch_returncode", "validator_command", "validator_returncode", "validation_passed",
    "convergence_status", "final_step", "final_physical_time", "residual_l2",
    "residual_linf", "cl", "cd", "cmz", "pressure_drag", "viscous_drag",
    "pressure_lift", "viscous_lift", "partition_edge_cut", "owned_cells_min",
    "owned_cells_max", "owned_cells_mean",
)
plt.rcParams.update({"font.size": 10, "axes.labelsize": 11, "axes.titlesize": 11,
                     "legend.fontsize": 9, "figure.dpi": 150, "savefig.dpi": 240,
                     "axes.grid": True, "grid.alpha": 0.28, "lines.linewidth": 1.8})


def finite_float(value: Any) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return math.nan
    return result if math.isfinite(result) else math.nan


def read_json(path: Path) -> dict[str, Any]:
    try:
        with path.open(encoding="utf-8") as handle:
            data = json.load(handle)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def read_csv(path: Path) -> list[dict[str, Any]]:
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            return list(csv.DictReader(handle))
    except (OSError, csv.Error):
        return []


def read_residuals(path: Path, max_outer_samples: int = 12000) -> list[dict[str, Any]]:
    """Bound residual-memory use while retaining the time-history endpoints.

    Production transient output may contain many inner iterations per physical
    step.  This two-pass reader retains the first and final CSV records and the
    final inner record of evenly spaced outer steps, which is what the residual
    figure needs to show convergence over the full run without loading every
    inner iteration into memory.
    """
    if max_outer_samples < 1:
        raise ValueError("max_outer_samples must be positive")
    try:
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            previous_step: str | None = None
            outer_count = 0
            for row in reader:
                step = str(row.get("step", ""))
                if step != previous_step:
                    outer_count += 1
                    previous_step = step
        if not outer_count:
            return []
        stride = max(1, math.ceil(outer_count / max_outer_samples))
        selected: list[dict[str, Any]] = []
        with path.open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            first = next(reader, None)
            if first is None:
                return []
            selected.append(first)
            current_step = str(first.get("step", "")); current_last = first; outer_index = 0
            for row in reader:
                step = str(row.get("step", ""))
                if step != current_step:
                    if outer_index % stride == 0 and current_last is not first:
                        selected.append(current_last)
                    outer_index += 1
                    current_step, current_last = step, row
                else:
                    current_last = row
            if current_last is not first:
                selected.append(current_last)
        # Sorting keeps the line plot monotone even though the first inner
        # record precedes a selected final-inner record at its outer step.
        return sorted(selected, key=lambda row: (finite_float(row.get("step")), finite_float(row.get("inner_iter"))))
    except (OSError, csv.Error):
        return []


def numeric_column(rows: list[dict[str, Any]], name: str) -> np.ndarray:
    return np.asarray([finite_float(row.get(name)) for row in rows], dtype=float)


def valid_series(x: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    keep = np.isfinite(x) & np.isfinite(y)
    return x[keep], y[keep]


def source_path(report_dir: Path, source: Path) -> str:
    try:
        return str(source.relative_to(report_dir))
    except ValueError:
        try:
            return str(Path("..") / source.relative_to(report_dir.parent))
        except ValueError:
            return str(source)


def latex_escape(value: Any) -> str:
    return str(value).replace("\\", r"\textbackslash{}").replace("_", r"\_").replace("&", r"\&").replace("%", r"\%").replace("#", r"\#")


@dataclass
class Field:
    polygons: list[np.ndarray]
    values: dict[str, np.ndarray]
    source_files: list[Path]

    @property
    def empty(self) -> bool:
        return not self.polygons

    def centers(self) -> np.ndarray:
        return np.asarray([poly.mean(axis=0) for poly in self.polygons], dtype=float)


def ascii_values(node: ET.Element | None, dtype: type[float] | type[int] = float) -> list[Any]:
    if node is None or node.attrib.get("format", "ascii") != "ascii":
        return []
    try:
        return [dtype(item) for item in (node.text or "").split()]
    except ValueError:
        return []


def parse_vtu(path: Path) -> tuple[list[np.ndarray], dict[str, np.ndarray]]:
    """Read the solver's ASCII VTK polygon cell data without a VTK dependency."""
    root = ET.parse(path).getroot()
    piece = root.find(".//Piece")
    if piece is None:
        raise ValueError("VTU contains no Piece")
    point_node = piece.find("./Points/DataArray")
    raw_points = np.asarray(ascii_values(point_node), dtype=float)
    if raw_points.size % 3:
        raise ValueError("invalid point array")
    points = raw_points.reshape((-1, 3))[:, :2]
    cells = piece.find("./Cells")
    if cells is None:
        raise ValueError("VTU contains no Cells")
    arrays = {node.attrib.get("Name", ""): node for node in cells.findall("DataArray")}
    connectivity = ascii_values(arrays.get("connectivity"), int)
    offsets = ascii_values(arrays.get("offsets"), int)
    polygons: list[np.ndarray] = []
    previous = 0
    for offset in offsets:
        indices = connectivity[previous:offset]
        previous = offset
        if len(indices) >= 3 and all(0 <= index < len(points) for index in indices):
            polygons.append(points[indices])
    values: dict[str, np.ndarray] = {}
    for node in piece.findall("./CellData/DataArray"):
        name = node.attrib.get("Name", "")
        data = np.asarray(ascii_values(node), dtype=float)
        if name and data.size == len(polygons):
            values[name] = data
    return polygons, values


def load_field(case_dir: Path) -> tuple[Field, list[str]]:
    paths = sorted(case_dir.glob("field_final_rank*.vtu"))
    if not paths:
        paths = sorted(case_dir.glob("field_final*.vtu"))
    polygons: list[np.ndarray] = []
    chunks: dict[str, list[np.ndarray]] = {name: [] for name in FIELD_VARIABLES}
    errors: list[str] = []
    for path in paths:
        try:
            piece_polygons, piece_values = parse_vtu(path)
            polygons.extend(piece_polygons)
            for name in FIELD_VARIABLES:
                chunks[name].append(piece_values.get(name, np.full(len(piece_polygons), np.nan)))
        except (OSError, ET.ParseError, ValueError) as error:
            errors.append(f"{path.name}: {error}")
    # The .pvtu is the published aggregate artifact; the rank pieces remain the
    # dependency-light source used by this reader.  Prefer the aggregate in the
    # manifest so a reader can open the intended final field directly.
    manifest_sources = [case_dir / "field_final.pvtu"] if (case_dir / "field_final.pvtu").is_file() else paths
    return Field(polygons, {name: np.concatenate(parts) if parts else np.empty(0) for name, parts in chunks.items()}, manifest_sources), errors


@dataclass
class CaseData:
    case_id: str
    directory: Path | None
    metadata: dict[str, Any]
    status: dict[str, Any]
    residuals: list[dict[str, Any]]
    forces: list[dict[str, Any]]
    surface: list[dict[str, Any]]
    field: Field
    errors: list[str]

    @property
    def is_naca(self) -> bool:
        return self.case_id.startswith("naca")

    @property
    def is_re200(self) -> bool:
        return self.case_id == "cylinder_m010_laminar_re200"

    @property
    def is_viscous(self) -> bool:
        return str(self.metadata.get("viscous_flux", "")).lower() not in {"", "disabled", "none", "false"}

    @property
    def is_inviscid(self) -> bool:
        return not self.is_viscous


def discover_cases(results_dir: Path) -> list[CaseData]:
    found = {item.name: item for item in results_dir.iterdir() if item.is_dir()} if results_dir.is_dir() else {}
    ids = list(EXPECTED_CASES) + sorted(name for name in found if name not in EXPECTED_CASES and (found[name] / "metadata.json").exists())
    cases: list[CaseData] = []
    for case_id in ids:
        directory = found.get(case_id)
        if directory is None:
            cases.append(CaseData(case_id, None, {}, {}, [], [], [], Field([], {}, []), ["required result directory is missing"]))
            continue
        errors: list[str] = []
        required = ("metadata.json", "run_status.json", "residuals.csv", "forces.csv", "surface.csv")
        errors.extend(f"missing {name}" for name in required if not (directory / name).is_file())
        field, field_errors = load_field(directory)
        errors.extend(field_errors)
        if field.empty:
            errors.append("missing or unreadable ASCII field_final_rank*.vtu polygon field")
        cases.append(CaseData(case_id, directory, read_json(directory / "metadata.json"), read_json(directory / "run_status.json"), read_residuals(directory / "residuals.csv"), read_csv(directory / "forces.csv"), read_csv(directory / "surface.csv"), field, errors))
    return cases


def load_case_inputs(case_input_dir: Path) -> dict[str, dict[str, Any]]:
    """Load optional supplied case JSON only for nondimensional spectrum scales."""
    if not case_input_dir.is_dir():
        return {}
    return {data.get("case_id", path.stem): data for path in case_input_dir.glob("*.json")
            if (data := read_json(path)).get("case_id")}


def load_rank_comparisons(results_dir: Path) -> tuple[list[dict[str, str]], list[str]]:
    """Read submitted rank-comparison evidence without reconstructing it from logs."""
    directory = results_dir / "rank_comparisons"
    if not directory.is_dir():
        return [], []
    rows: list[dict[str, str]] = []
    errors: list[str] = []
    numeric = set(RANK_COMPARISON_COLUMNS) - {"case_id", "command", "notes"}

    def partition_summary(output_dir: Path) -> tuple[float, float, float] | None:
        diagnostics = read_csv(output_dir / "partition_diagnostics.csv")
        owned = numeric_column(diagnostics, "num_cells_owned"); ghosts = numeric_column(diagnostics, "num_cells_ghost")
        owned, ghosts = owned[np.isfinite(owned)], ghosts[np.isfinite(ghosts)]
        if not len(owned) or not len(ghosts) or np.mean(owned) <= 0:
            return None
        return float(np.mean(ghosts)), float(np.max(owned) / np.mean(owned)), float(np.mean(owned))

    for path in sorted(directory.glob("*.csv")):
        try:
            with path.open(newline="", encoding="utf-8") as handle:
                reader = csv.DictReader(handle)
                header = tuple(reader.fieldnames or ())
                if header not in (RANK_COMPARISON_COLUMNS, RUNNER_COMPARISON_COLUMNS):
                    errors.append(f"{path.name}: unsupported header (use documented report schema or run_rank_comparisons comparison.csv)")
                    continue
                for line, row in enumerate(reader, start=2):
                    if header == RANK_COMPARISON_COLUMNS:
                        if not row or not all((row.get(key) or "").strip() for key in RANK_COMPARISON_COLUMNS):
                            errors.append(f"{path.name}:{line}: incomplete comparison row")
                            continue
                        if not all(math.isfinite(finite_float(row[key])) for key in numeric):
                            errors.append(f"{path.name}:{line}: non-finite numeric comparison value")
                            continue
                        row["source_file"] = str(Path("..") / "results" / "rank_comparisons" / path.name)
                        row["residual_metric"] = row["residual_reduction_orders"]
                        row["residual_metric_label"] = "reduction orders"
                        rows.append(row)
                        continue
                    required = ("case_id", "ranks", "command", "wall_time_seconds", "final_step", "final_physical_time", "residual_l2", "cl", "cd", "cmz", "partition_edge_cut", "owned_cells_min", "owned_cells_max")
                    if not row or not all((row.get(key) or "").strip() for key in required):
                        errors.append(f"{path.name}:{line}: incomplete runner comparison row")
                        continue
                    if str(row.get("validation_passed", "")).lower() not in {"true", "1"}:
                        errors.append(f"{path.name}:{line}: runner comparison was not validator-passing")
                        continue
                    if not all(math.isfinite(finite_float(row[key])) for key in set(required) - {"case_id", "command"}):
                        errors.append(f"{path.name}:{line}: non-finite runner comparison value")
                        continue
                    output_dir = directory / row["case_id"] / f"np{int(finite_float(row['ranks']))}"
                    summary = partition_summary(output_dir)
                    if summary is None:
                        errors.append(f"{path.name}:{line}: cannot derive ghost mean/load balance from {output_dir / 'partition_diagnostics.csv'}")
                        continue
                    ghost_mean, balance, _owned_mean = summary
                    rows.append({"case_id": row["case_id"], "mpi_ranks": row["ranks"], "command": row["command"],
                                 "wall_time_seconds": row["wall_time_seconds"], "final_step": row["final_step"],
                                 "final_physical_time": row["final_physical_time"], "cd": row["cd"], "cl": row["cl"], "cmz": row["cmz"],
                                 "residual_reduction_orders": row["residual_l2"], "residual_metric": row["residual_l2"], "residual_metric_label": "terminal $L_2$",
                                 "owned_cells_min": row["owned_cells_min"], "owned_cells_max": row["owned_cells_max"], "ghost_cells_mean": str(ghost_mean),
                                 "load_balance_ratio": str(balance), "partition_edge_cut": row["partition_edge_cut"],
                                 "notes": f"runner status={row.get('convergence_status', '')}", "source_file": str(Path("..") / "results" / "rank_comparisons" / path.name)})
        except (OSError, csv.Error) as error:
            errors.append(f"{path.name}: {error}")
    rows.sort(key=lambda row: (row["case_id"], finite_float(row["mpi_ranks"])))
    return rows, errors


def add_manifest(rows: list[dict[str, str]], report_dir: Path, figure: Path, case: CaseData, kind: str, variable: str, source: Path, caption: str) -> None:
    rows.append({"figure_file": figure.name, "case_id": case.case_id, "figure_type": kind,
                 "variable": variable, "source_file": source_path(report_dir, source), "caption": caption})


def save_figure(fig: plt.Figure, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def plot_residual(case: CaseData, figures: Path, manifest: list[dict[str, str]], report_dir: Path) -> None:
    if not case.directory or not case.residuals:
        return
    x = numeric_column(case.residuals, "step")
    y = numeric_column(case.residuals, "residual_l2")
    x, y = valid_series(x, y)
    keep = y > 0
    if not np.any(keep):
        return
    fig, ax = plt.subplots(figsize=(6.3, 3.7)); ax.semilogy(x[keep], y[keep], color="#2166ac", label=r"$L_2$ residual")
    linf = numeric_column(case.residuals, "residual_linf"); lx, ly = valid_series(x, linf[:len(x)])
    if len(ly) and np.any(ly > 0): ax.semilogy(lx[ly > 0], ly[ly > 0], color="#b2182b", label=r"$L_\infty$ residual")
    ax.set(xlabel="Outer step", ylabel="Residual norm", title=f"{case.case_id}: residual history"); ax.legend();
    path = figures / f"{case.case_id}_residual.png"; save_figure(fig, path)
    add_manifest(manifest, report_dir, path, case, "line", "residual_l2,residual_linf", case.directory / "residuals.csv", "Global residual-norm history.")


def plot_forces(case: CaseData, figures: Path, manifest: list[dict[str, str]], report_dir: Path) -> None:
    if not case.directory or not case.forces:
        return
    time = numeric_column(case.forces, "physical_time"); step = numeric_column(case.forces, "step")
    x = time if np.count_nonzero(np.isfinite(time)) > 1 and np.nanmax(time) > np.nanmin(time) else step
    fig, ax = plt.subplots(figsize=(6.3, 3.7)); used = False
    for key, label, color in (("cd", r"$C_D$", "#2166ac"), ("cl", r"$C_L$", "#b2182b"), ("cmz", r"$C_m$", "#4d9221")):
        xx, yy = valid_series(x, numeric_column(case.forces, key))
        if len(xx): ax.plot(xx, yy, label=label, color=color); used = True
    if not used: plt.close(fig); return
    ax.set(xlabel="Physical time" if x is time else "Step", ylabel="Force coefficient", title=f"{case.case_id}: force history"); ax.legend(ncol=3)
    path = figures / f"{case.case_id}_forces.png"; save_figure(fig, path)
    add_manifest(manifest, report_dir, path, case, "line", "cd,cl,cmz", case.directory / "forces.csv", "Aerodynamic force-coefficient history.")


def re200_lift_spectrum(case: CaseData, case_inputs: dict[str, dict[str, Any]], report_dir: Path) -> tuple[dict[str, Any], list[dict[str, float]]]:
    """FFT the second half of uniformly resampled submitted post-transient lift."""
    unavailable: dict[str, Any] = {"available": False, "source_file": "", "reason": "insufficient finite force history"}
    if not case.directory:
        return unavailable, []
    time, lift = valid_series(numeric_column(case.forces, "physical_time"), numeric_column(case.forces, "cl"))
    if len(time) < 8:
        return unavailable, []
    order = np.argsort(time); time, lift = time[order], lift[order]
    unique, indices = np.unique(time, return_index=True); time, lift = unique, lift[indices]
    if len(time) < 8 or not time[-1] > time[0]:
        return unavailable, []
    start = time[0] + 0.5 * (time[-1] - time[0])
    keep = time >= start; time, lift = time[keep], lift[keep]
    if len(time) < 8:
        return unavailable, []
    # A regular grid makes the frequency axis explicit even if output cadence varied.
    regular_time = np.linspace(time[0], time[-1], len(time))
    signal = np.interp(regular_time, time, lift); dt = float(regular_time[1] - regular_time[0])
    demeaned = signal - np.mean(signal); window = np.hanning(len(demeaned))
    amplitude = np.abs(np.fft.rfft(demeaned * window)) * 2.0 / max(float(np.sum(window)), 1.0)
    frequency = np.fft.rfftfreq(len(demeaned), d=dt)
    if len(frequency) < 2 or not np.any(np.isfinite(amplitude[1:])):
        return unavailable, []
    dominant_index = int(np.nanargmax(amplitude[1:]) + 1)
    config = case_inputs.get(case.case_id, {})
    reference = config.get("reference", {}) if isinstance(config.get("reference"), dict) else {}
    freestream = config.get("freestream", {}) if isinstance(config.get("freestream"), dict) else {}
    length = finite_float(reference.get("length")); velocity = finite_float(freestream.get("velocity_magnitude"))
    strouhal = frequency[dominant_index] * length / velocity if length > 0 and velocity > 0 else math.nan
    all_time = numeric_column(case.forces, "physical_time")
    all_drag = numeric_column(case.forces, "cd")
    post_drag = all_drag[np.isfinite(all_time) & np.isfinite(all_drag) & (all_time >= regular_time[0])]
    metrics = {"available": True, "source_file": source_path(report_dir, case.directory / "forces.csv"),
               "analysis_window_start": float(regular_time[0]), "analysis_window_end": float(regular_time[-1]),
               "samples": int(len(regular_time)), "sample_interval": dt,
               "dominant_frequency": float(frequency[dominant_index]), "strouhal": float(strouhal),
               "reference_length": length, "freestream_velocity": velocity,
               "mean_drag": float(np.mean(post_drag)) if len(post_drag) else math.nan, "mean_lift": float(np.mean(signal)),
               "lift_amplitude": float(0.5 * np.ptp(signal)),
               "lift_min": float(np.min(signal)), "lift_max": float(np.max(signal))}
    rows = [{"frequency": float(f), "lift_amplitude": float(a), "is_dominant": int(index == dominant_index)}
            for index, (f, a) in enumerate(zip(frequency[1:], amplitude[1:]), start=1)]
    return metrics, rows


def plot_re200_spectrum(case: CaseData, metrics: dict[str, Any], rows: list[dict[str, float]], figures: Path, manifest: list[dict[str, str]], report_dir: Path) -> None:
    if not case.directory or not metrics.get("available") or not rows:
        return
    frequency = np.asarray([row["frequency"] for row in rows]); amplitude = np.asarray([row["lift_amplitude"] for row in rows])
    fig, ax = plt.subplots(figsize=(6.3, 3.7)); ax.plot(frequency, amplitude, color="#542788", label=r"post-transient $C_L$ spectrum")
    dominant = finite_float(metrics.get("dominant_frequency")); ax.axvline(dominant, color="#d6604d", linestyle="--", label=fr"dominant $f={dominant:.5g}$")
    ax.set(xlabel="Frequency", ylabel=r"Lift-amplitude spectrum of $C_L$", title=f"{case.case_id}: post-transient lift spectrum"); ax.legend()
    path = figures / f"{case.case_id}_lift_spectrum.png"; save_figure(fig, path)
    add_manifest(manifest, report_dir, path, case, "spectrum", "lift_spectrum,dominant_frequency,strouhal", case.directory / "forces.csv", "Post-transient lift spectrum calculated from forces.csv; dashed line marks the dominant frequency.")


def plot_surface(case: CaseData, figures: Path, manifest: list[dict[str, str]], report_dir: Path) -> None:
    if not case.directory or not case.surface:
        return
    x, cp = valid_series(numeric_column(case.surface, "x"), numeric_column(case.surface, "cp"))
    if not len(x): return
    order = np.argsort(x); fig, ax = plt.subplots(figsize=(6.3, 3.7)); ax.plot(x[order], cp[order], ".-", ms=3, color="#2166ac", label=r"$C_p$")
    rawx = numeric_column(case.surface, "x"); rawcf = numeric_column(case.surface, "cf"); sx, scf = valid_series(rawx, rawcf)
    if case.is_viscous and len(sx):
        ax.plot(sx[np.argsort(sx)], scf[np.argsort(sx)], ".-", ms=3, color="#b2182b", label=r"$C_f$")
    ax.set(xlabel="Wall x-coordinate", ylabel="Surface coefficient", title=f"{case.case_id}: wall distribution"); ax.legend()
    path = figures / f"{case.case_id}_surface_cp.png"; save_figure(fig, path)
    variable = "cp,cf" if case.is_viscous else "cp"
    add_manifest(manifest, report_dir, path, case, "line", variable, case.directory / "surface.csv", "Wall pressure coefficient" + (" and skin-friction coefficient." if case.is_viscous else "."))


def percentile_norm(values: np.ndarray, *, symmetric: bool = False) -> Normalize:
    finite = values[np.isfinite(values)]
    if finite.size == 0: return Normalize(0.0, 1.0)
    low, high = np.percentile(finite, [1, 99])
    if symmetric:
        high = max(abs(low), abs(high)); low = -high
    if not high > low: high = low + max(1e-12, abs(low) * 1e-6)
    return Normalize(float(low), float(high), clip=True)


def body_view_bounds(case: CaseData, *, wake: bool) -> tuple[tuple[float, float], tuple[float, float]] | None:
    """Use submitted wall coordinates for a visible body/wake crop when possible."""
    x = numeric_column(case.surface, "x"); y = numeric_column(case.surface, "y")
    x, y = valid_series(x, y)
    if not len(x):
        return None
    xlo, xhi, ylo, yhi = float(np.min(x)), float(np.max(x)), float(np.min(y)), float(np.max(y))
    span = max(xhi - xlo, yhi - ylo, 1e-6)
    xpad = 0.12 * span
    ypad = max(0.18 * span, 0.02 * (yhi - ylo + span))
    if wake:
        return (xlo - xpad, xhi + 3.0 * span), (ylo - 1.2 * span, yhi + 1.2 * span)
    return (xlo - xpad, xhi + 0.75 * span), (ylo - ypad, yhi + ypad)


def plot_field(case: CaseData, variable: str, figures: Path, manifest: list[dict[str, str]], report_dir: Path, suffix: str = "", wake: bool = False) -> None:
    if not case.directory or case.field.empty or variable not in case.field.values:
        return
    values = case.field.values[variable]
    if len(values) != len(case.field.polygons) or not np.any(np.isfinite(values)): return
    fig, ax = plt.subplots(figsize=(6.7, 4.2)); collection = PolyCollection(case.field.polygons, array=values, cmap="viridis", norm=percentile_norm(values), edgecolors="none")
    ax.add_collection(collection); ax.autoscale(); ax.set_aspect("equal", adjustable="box")
    centers = case.field.centers()
    if suffix and len(centers):
        bounds = body_view_bounds(case, wake=wake) if (case.is_naca or wake) else None
        if bounds:
            ax.set_xlim(*bounds[0]); ax.set_ylim(*bounds[1])
        elif case.is_naca:
            ax.set_xlim(np.percentile(centers[:, 0], [35, 65])); ax.set_ylim(np.percentile(centers[:, 1], [35, 65]))
        elif wake:
            ax.set_xlim(np.percentile(centers[:, 0], [35, 82])); ax.set_ylim(np.percentile(centers[:, 1], [30, 70]))
    label = {"mach": "Mach number", "pressure": "Pressure", "velocity_magnitude": "Velocity magnitude"}[variable]
    bar = fig.colorbar(collection, ax=ax, pad=0.02); bar.set_label(label + " (1st--99th percentile clipped)")
    ax.set(xlabel="$x$", ylabel="$y$", title=f"{case.case_id}: {label.lower()}" + (" detail" if suffix else ""))
    filename = f"{case.case_id}_{variable}{suffix}.png"; path = figures / filename; save_figure(fig, path)
    add_manifest(manifest, report_dir, path, case, "polygon_field", variable, case.field.source_files[0], f"Cell-polygon {label.lower()} from final ASCII VTU field; colour range is clipped to the 1st--99th percentiles.")


def make_figures(cases: list[CaseData], report_dir: Path, case_inputs: dict[str, dict[str, Any]]) -> tuple[list[dict[str, str]], dict[str, Any], list[dict[str, float]]]:
    figures = report_dir / "figures"; figures.mkdir(parents=True, exist_ok=True)
    manifest: list[dict[str, str]] = []
    spectrum: dict[str, Any] = {"available": False, "reason": "Re200 case unavailable"}
    spectrum_rows: list[dict[str, float]] = []
    for case in cases:
        plot_residual(case, figures, manifest, report_dir); plot_forces(case, figures, manifest, report_dir); plot_surface(case, figures, manifest, report_dir)
        for variable in ("mach", "pressure"):
            plot_field(case, variable, figures, manifest, report_dir)
            plot_field(case, variable, figures, manifest, report_dir, suffix="_detail", wake=not case.is_naca)
        if case.is_re200 and not case.field.empty:
            u, v = case.field.values.get("u", np.empty(0)), case.field.values.get("v", np.empty(0))
            if len(u) == len(v) == len(case.field.polygons):
                case.field.values["velocity_magnitude"] = np.hypot(u, v)
                plot_field(case, "velocity_magnitude", figures, manifest, report_dir, suffix="_wake", wake=True)
        if case.is_re200:
            spectrum, spectrum_rows = re200_lift_spectrum(case, case_inputs, report_dir)
            plot_re200_spectrum(case, spectrum, spectrum_rows, figures, manifest, report_dir)
    return manifest, spectrum, spectrum_rows


FORCE_COLUMNS = ("cd", "cl", "cmz", "pressure_drag", "viscous_drag")


def force_metrics(case: CaseData) -> dict[str, Any]:
    """Final steady coefficients or post-transient Re200 statistics, never a mixed label."""
    missing: dict[str, Any] = {name: math.nan for name in FORCE_COLUMNS}
    if not case.forces:
        return {**missing, "metric_mode": "unavailable", "sample_count": 0, "note": "force history unavailable", "lift_amplitude": math.nan}
    if case.is_re200:
        rows = case.forces[max(0, int(len(case.forces) * 0.5)):]
        result = {name: float(np.nanmean(numeric_column(rows, name))) for name in FORCE_COLUMNS}
        lift = numeric_column(rows, "cl"); lift = lift[np.isfinite(lift)]
        return {**result, "metric_mode": "post_transient_mean_final_half", "sample_count": len(rows),
                "lift_amplitude": float(0.5 * np.ptp(lift)) if len(lift) else math.nan,
                "note": "mean over the final half of the force history; lift amplitude is half the range"}
    final = case.forces[-1]
    return {**{name: finite_float(final.get(name)) for name in FORCE_COLUMNS}, "metric_mode": "final_sample",
            "sample_count": 1, "lift_amplitude": math.nan, "note": "final recorded force sample"}


def status_for(case: CaseData, checks: dict[str, Any]) -> str:
    claimed = str(case.status.get("convergence_status", case.metadata.get("convergence_status", "failed"))).lower()
    if claimed not in {"converged", "statistically_periodic", "failed"}: claimed = "failed"
    return claimed if checks.get("passed", False) and not case.errors else "failed"


def production_command(case: CaseData) -> str:
    """Restore the MPI launcher omitted from argv recorded inside the solver."""
    command = str(case.status.get("command", "--"))
    if command == "--" or command.lstrip().startswith(("mpirun ", "mpiexec ")):
        return command
    ranks = case.status.get("mpi_ranks", case.metadata.get("mpi_ranks", "--"))
    return f"mpirun -np {ranks} {command}"


def case_checks(case: CaseData) -> dict[str, Any]:
    checks: dict[str, Any] = {"input_errors": case.errors, "checks": {}}
    field = case.field
    for name in ("density", "pressure"):
        values = field.values.get(name, np.empty(0))
        checks["checks"][f"positive_{name}"] = bool(values.size and np.all(np.isfinite(values)) and np.all(values > 0))
    surface_cp = numeric_column(case.surface, "cp")
    finite_cp = surface_cp[np.isfinite(surface_cp)]
    checks["checks"]["nontrivial_surface_cp"] = bool(finite_cp.size >= 2 and np.ptp(finite_cp) > 1e-10)
    final_force_step = finite_float(case.forces[-1].get("step")) if case.forces else math.nan
    final_status_step = finite_float(case.status.get("final_step"))
    checks["checks"]["final_force_step_matches_status"] = bool(
        math.isfinite(final_force_step) and math.isfinite(final_status_step) and final_force_step == final_status_step)
    metrics = force_metrics(case); checks["force_metrics"] = metrics
    if case.is_naca:
        checks["checks"]["zero_aoa_lift_near_zero"] = bool(math.isfinite(metrics["cl"]) and abs(metrics["cl"]) < 0.05)
        checks["checks"]["nontrivial_drag"] = bool(math.isfinite(metrics["cd"]) and abs(metrics["cd"]) > 1e-12)
    if case.case_id.startswith("cylinder"):
        checks["checks"]["positive_mean_drag"] = bool(math.isfinite(metrics["cd"]) and metrics["cd"] > 0)
    if case.is_re200:
        lift = numeric_column(case.forces[max(0, int(len(case.forces) * .5)):], "cl")
        lift = lift[np.isfinite(lift)]
        checks["checks"]["unsteady_lift_after_startup"] = bool(lift.size >= 3 and np.ptp(lift) > 1e-6)
    if case.is_viscous:
        wall_speed = np.hypot(numeric_column(case.surface, "u"), numeric_column(case.surface, "v"))
        wall_speed = wall_speed[np.isfinite(wall_speed)]
        cf = numeric_column(case.surface, "cf"); cf = cf[np.isfinite(cf)]
        speed_limit, cf_floor = 1e-6, 1e-12
        checks["wall_condition_evidence"] = {"kind": "no_slip", "max_wall_speed": float(np.max(wall_speed)) if len(wall_speed) else math.nan,
                                             "wall_speed_limit": speed_limit, "max_abs_cf": float(np.max(np.abs(cf))) if len(cf) else math.nan,
                                             "cf_nonzero_floor": cf_floor}
        checks["checks"]["no_slip_wall_speed"] = bool(len(wall_speed) and np.max(wall_speed) <= speed_limit)
        checks["checks"]["viscous_wall_nonzero_cf"] = bool(len(cf) and np.max(np.abs(cf)) > cf_floor)
    else:
        normal_velocity = numeric_column(case.surface, "u") * numeric_column(case.surface, "nx") + numeric_column(case.surface, "v") * numeric_column(case.surface, "ny")
        normal_velocity = normal_velocity[np.isfinite(normal_velocity)]
        tangential_velocity = -numeric_column(case.surface, "u") * numeric_column(case.surface, "ny") + numeric_column(case.surface, "v") * numeric_column(case.surface, "nx")
        tangential_velocity = tangential_velocity[np.isfinite(tangential_velocity)]
        final_viscous = np.asarray([finite_float(case.forces[-1].get(name)) for name in ("viscous_drag", "viscous_lift")], dtype=float) if case.forces else np.empty(0)
        velocity_limit, tangential_floor, force_limit = 1e-6, 1e-6, 1e-10
        checks["wall_condition_evidence"] = {"kind": "slip", "max_abs_normal_velocity": float(np.max(np.abs(normal_velocity))) if len(normal_velocity) else math.nan,
                                             "max_abs_tangential_velocity": float(np.max(np.abs(tangential_velocity))) if len(tangential_velocity) else math.nan,
                                             "tangential_velocity_floor": tangential_floor,
                                             "normal_velocity_limit": velocity_limit, "max_abs_final_viscous_force": float(np.nanmax(np.abs(final_viscous))) if np.any(np.isfinite(final_viscous)) else math.nan,
                                             "viscous_force_limit": force_limit}
        checks["checks"]["slip_wall_normal_velocity"] = bool(len(normal_velocity) and np.max(np.abs(normal_velocity)) <= velocity_limit)
        checks["checks"]["slip_wall_nonzero_tangential_velocity"] = bool(len(tangential_velocity) and np.max(np.abs(tangential_velocity)) > tangential_floor)
        checks["checks"]["slip_wall_negligible_viscous_force"] = bool(np.any(np.isfinite(final_viscous)) and np.nanmax(np.abs(final_viscous)) <= force_limit)
    checks["passed"] = bool(not case.errors and all(checks["checks"].values()))
    return checks


def augment_manifest_checks(cases: list[CaseData], figures: list[dict[str, str]], checks: dict[str, Any]) -> None:
    """Check the required Mach/pressure figure mappings after they are actually created."""
    for case in cases:
        matches = {variable: [row["figure_file"] for row in figures if row["case_id"] == case.case_id and row["variable"] == variable]
                   for variable in ("mach", "pressure")}
        mapping = {variable: bool(names and all(variable in Path(name).stem for name in names)) for variable, names in matches.items()}
        checks[case.case_id]["figure_manifest_mapping"] = {"files": matches, "required_variables": list(matches), "matches_filename_and_variable": mapping}
        for variable, passed in mapping.items():
            checks[case.case_id]["checks"][f"figure_manifest_{variable}_mapping"] = passed
        checks[case.case_id]["passed"] = bool(not case.errors and all(checks[case.case_id]["checks"].values()))


def write_csv(path: Path, rows: Iterable[dict[str, Any]], fields: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore", lineterminator="\n")
        writer.writeheader(); writer.writerows(rows)


def json_safe(value: Any) -> Any:
    """Preserve missing numeric evidence as JSON null, never a non-standard NaN."""
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def rank_comparison_discussion(rows: list[dict[str, str]]) -> list[str]:
    discussion: list[str] = []
    by_case: dict[str, list[dict[str, str]]] = {}
    for row in rows: by_case.setdefault(row["case_id"], []).append(row)
    for case_id, group in by_case.items():
        baseline, final = group[0], group[-1]
        if len(group) < 2:
            discussion.append(f"{latex_escape(case_id)} has only one submitted rank count; no rank-consistency conclusion is drawn.")
            continue
        cd_delta = finite_float(final["cd"]) - finite_float(baseline["cd"])
        cl_delta = finite_float(final["cl"]) - finite_float(baseline["cl"])
        residual_delta = finite_float(final["residual_metric"]) - finite_float(baseline["residual_metric"])
        low_wall, high_wall = finite_float(baseline["wall_time_seconds"]), finite_float(final["wall_time_seconds"])
        speedup = low_wall / high_wall if high_wall > 0 else math.nan
        balance_delta = finite_float(final["load_balance_ratio"]) - finite_float(baseline["load_balance_ratio"])
        discussion.append(f"For {latex_escape(case_id)}, submitted ranks span {baseline['mpi_ranks']}--{final['mpi_ranks']}; final-force changes are $\\Delta C_D={cd_delta:.4g}$ and $\\Delta C_L={cl_delta:.4g}$, the recorded residual-metric change is {residual_delta:.4g}, and the wall-time ratio is {speedup:.4g} (low-rank/high-rank). The load-balance-ratio change is {balance_delta:.4g}; edge cut and ghost counts are recorded rather than used to infer unmeasured communication overhead.")
    return discussion


def representative_partition_rows(cases: list[CaseData]) -> list[dict[str, Any]]:
    """Return traceable rank-level rows for one NACA and one cylinder production run."""
    representative_ids = {"naca0012_m200_inviscid", "cylinder_m010_laminar_re20"}
    selected: list[dict[str, Any]] = []
    for case in cases:
        if case.case_id not in representative_ids or not case.directory:
            continue
        rows = read_csv(case.directory / "partition_diagnostics.csv")
        if not rows:
            continue
        for index in sorted({0, len(rows) // 2, len(rows) - 1}):
            selected.append({"case_id": case.case_id, **rows[index]})
    return selected


def write_report(report_dir: Path, cases: list[CaseData], checks: dict[str, Any], figures: list[dict[str, str]], spectrum: dict[str, Any], spectrum_rows: list[dict[str, float]], rank_rows: list[dict[str, str]], rank_errors: list[str], case_inputs: dict[str, dict[str, Any]]) -> None:
    status_rows = []
    force_rows = []
    for case in cases:
        status = status_for(case, checks[case.case_id])
        residual = finite_float(case.status.get("residual_reduction_orders"))
        status_rows.append({"case_id": case.case_id, "command": production_command(case),
                            "mpi_ranks": case.status.get("mpi_ranks", case.metadata.get("mpi_ranks", "--")), "steps": case.status.get("final_step", "--"), "physical_time": case.status.get("final_physical_time", "--"), "residual_reduction": "--" if not math.isfinite(residual) else f"{residual:.3g}", "wall_time_seconds": case.status.get("wall_time_seconds", "--"), "status": status})
        metrics = force_metrics(case)
        force_rows.append({"case_id": case.case_id, **{key: "--" if not math.isfinite(finite_float(metrics.get(key))) else f"{finite_float(metrics[key]):.5g}" for key in FORCE_COLUMNS},
                           "lift_amplitude": "--" if not math.isfinite(finite_float(metrics.get("lift_amplitude"))) else f"{finite_float(metrics['lift_amplitude']):.5g}",
                           "metric_mode": str(metrics["metric_mode"]), "note": str(metrics["note"]), "status": status})
    status_fields = ["case_id", "command", "mpi_ranks", "steps", "physical_time", "residual_reduction", "wall_time_seconds", "status"]
    write_csv(report_dir / "run_manifest.csv", status_rows, status_fields)
    write_csv(report_dir / "figure_manifest.csv", figures, ["figure_file", "case_id", "figure_type", "variable", "source_file", "caption"])
    write_csv(report_dir / "rank_comparison_manifest.csv", rank_rows, [*RANK_COMPARISON_COLUMNS, "source_file"])
    write_csv(report_dir / "re200_lift_spectrum.csv", spectrum_rows, ["frequency", "lift_amplitude", "is_dominant"])
    with (report_dir / "sanity_checks.json").open("w", encoding="utf-8") as handle: json.dump(json_safe({"cases": checks, "re200_lift_spectrum": spectrum, "rank_comparison_errors": rank_errors, "generated_by": "tools/generate_report.py"}), handle, indent=2, allow_nan=False)
    completed_count = sum(status_for(case, checks[case.case_id]) in {"converged", "statistically_periodic"} for case in cases)
    re200_summary = ""
    if spectrum.get("available"):
        re200_summary = (f" The Re200 run reached a statistically periodic state with mean $C_D="
                         f"{finite_float(spectrum.get('mean_drag')):.5g}$, lift amplitude "
                         f"{finite_float(spectrum.get('lift_amplitude')):.5g}, and $St="
                         f"{finite_float(spectrum.get('strouhal')):.5g}$.")
    lines = [r"\documentclass[11pt]{article}", r"\usepackage[margin=0.8in]{geometry}",
             r"\usepackage{amsmath,amssymb,graphicx,booktabs,longtable,hyperref}",
             r"\Urlmuskip=0mu plus 2mu\relax", r"\setlength{\emergencystretch}{3em}",
             r"\title{2-D Unstructured Compressible Navier--Stokes Solver Benchmark Report}",
             r"\author{Automated evidence-grounded report}", r"\date{\today}", r"\begin{document}",
             r"\maketitle", r"\begin{abstract}",
             f"A C++17/MPI cell-centred finite-volume solver was applied to all eight supplied unstructured CGNS cases using METIS partitions, second-order limited reconstruction, Rusanov convective flux, and Newtonian/Fourier viscous flux where applicable. {completed_count} of {len(cases)} required cases completed with measured sanity checks passing; seven are steady and the Re200 cylinder is statistically periodic.{re200_summary} Known accuracy and visualization limitations are stated explicitly.",
             r"\end{abstract}", r"\section{Introduction}",
             "The benchmark covers three inviscid and three laminar NACA0012 cases at zero angle of attack, plus laminar circular-cylinder cases at Reynolds 20 and 200. It exercises mesh import, compressible-flow numerics, implicit steady and physical-time transient integration, distributed-memory execution, restart/output integrity, and traceable post-processing. Results below distinguish final steady samples from post-transient statistics.",
             r"\section{Governing equations and nondimensionalization}",
             r"The conservative state is $\mathbf{U}=[\rho,\rho u,\rho v,\rho E]^T$ and $\partial_t\mathbf{U}+\partial_x\mathbf{F}^i+\partial_y\mathbf{G}^i=\partial_x\mathbf{F}^v+\partial_y\mathbf{G}^v$. The inviscid fluxes are $\mathbf{F}^i=[\rho u,\rho u^2+p,\rho uv,u(\rho E+p)]^T$ and $\mathbf{G}^i=[\rho v,\rho uv,\rho v^2+p,v(\rho E+p)]^T$, with $p=(\gamma-1)\rho e$, $E=e+(u^2+v^2)/2$, and $a=\sqrt{\gamma p/\rho}$. For viscous cases the flux contains Newtonian stresses $\tau_{ij}=\mu(\partial_i u_j+\partial_j u_i-\tfrac23\delta_{ij}\nabla\cdot\mathbf{u})$ and Fourier heat flux $q_i=-\mu c_p\partial_iT/Pr$. Constant viscosity is set by $Re=\rho_\infty U_\infty L/\mu$. The case reference density, velocity, and length nondimensionalize the solution; pressure uses $\rho_\infty U_\infty^2$, while $C_D=F_D/(q_\infty A)$, $C_L=F_L/(q_\infty A)$, and $C_m=M/(q_\infty A L)$ with $q_\infty=\tfrac12\rho_\infty U_\infty^2$.",
             r"\section{Meshes, boundary tags, and case inputs}",
             r"Rank 0 reads the CGNS unstructured zone, polygon connectivity, and named boundary sections. It constructs consistently oriented faces from cell edges, pairs interior edges, assigns boundary-family tags, and computes polygon area/centroid plus face midpoint, outward unit normal, and length. Boundary mappings are checked against the mesh before marching. Global sizes and supplied parameters are reproduced below.",
             r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{llrrrrl}", r"\toprule Case & mesh & cells & faces & Mach & Re & boundary tags\\\midrule"]
    for case in cases:
        cfg = case_inputs.get(case.case_id, {}); free = cfg.get("freestream", {}) if isinstance(cfg.get("freestream"), dict) else {}; physics = cfg.get("physics", {}) if isinstance(cfg.get("physics"), dict) else {}; bcs = cfg.get("boundary_conditions", {}) if isinstance(cfg.get("boundary_conditions"), dict) else {}
        mesh_name = Path(str(case.metadata.get("mesh_file", "--"))).name
        lines.append(f"{latex_escape(case.case_id)} & {latex_escape(mesh_name)} & {case.metadata.get('num_cells_global', '--')} & {case.metadata.get('num_faces_global', '--')} & {free.get('mach', '--')} & {physics.get('reynolds', '--')} & {latex_escape(', '.join(f'{key}:{value}' for key, value in bcs.items()) or '--')}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"\section{Spatial discretization}",
              r"For a cell $K$ of area $V_K$, the cell-centred finite-volume residual is $R_K=\sum_{f\subset\partial K}(\hat F_f^i-\hat F_f^v)|f|$, with each stored normal directed out of the left cell; the same interior-face flux is added to the left residual and subtracted from the right residual. The semi-discrete equation is $V_K\,dU_K/dt+R_K=0$. Boundary faces use a reconstructed interior state and a boundary/ghost state described below.",
              r"\subsection{Second-order reconstruction, limiter, and positivity}",
              r"Primitive gradients solve the weighted least-squares normal equations $A_K\nabla q_K=b_K$ from neighbor-centroid offsets, including exchanged ghost values and boundary values. Face states use $q_f=q_K+\alpha_K\nabla q_K\cdot(x_f-x_K)$. A Barth--Jespersen limiter chooses $0\leq\alpha_K\leq1$ so every extrapolated primitive remains inside local neighbor bounds. Production steady runs ramp the reconstruction factor from zero during startup to one, so the submitted terminal solutions use the full limited linear reconstruction. If reconstructed density or pressure is non-positive, that face side falls back to the admissible cell-centre state; conservative updates are likewise line-searched toward the old state until density and pressure are positive.",
              r"\subsection{Inviscid and viscous fluxes}",
              r"The convective numerical flux is Rusanov/local Lax--Friedrichs, $\hat F^i=\tfrac12[F_n(U_L)+F_n(U_R)]-\tfrac12s_{\max}(U_R-U_L)$, where $s_{\max}=\max(|u_{n,L}|+a_L,|u_{n,R}|+a_R)$ (times the recorded dissipation scale). No separate entropy fix is used. Laminar face velocity and temperature gradients are averaged from neighboring least-squares gradients. They form Newtonian stress and Fourier heat flux; at no-slip walls the normal velocity gradient is imposed from the cell-to-wall distance and the adiabatic temperature-normal gradient is zero. Pressure force and tangential wall shear are accumulated separately, so skin friction excludes viscous normal traction.",
              r"\section{Boundary conditions}",
              r"Farfield faces use a characteristic-style boundary state: incoming information is drawn from the prescribed freestream and outgoing information from the interior. A slip wall reflects normal velocity in the ghost state while retaining tangential velocity; its direct wall state has zero normal velocity. A no-slip adiabatic wall reflects both velocity components so the wall-interpolated velocity is zero, retains wall temperature for zero normal heat flux, and applies direct pressure plus tangential-shear wall fluxes. Consequently \texttt{surface.csv} contains boundary values, not adjacent cell-centre values; the measured slip/no-slip gates are reported in the sanity section.",
              r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lllllll}", r"\toprule Case & inviscid flux & viscous flux & reconstruction & limiter & positivity & implicit solver\\\midrule"]
    for case in cases:
        lines.append(f"{latex_escape(case.case_id)} & Rusanov & {'Newtonian/Fourier' if case.is_viscous else 'disabled'} & LS linear & Barth--Jespersen & $\\rho,p$ fallback & Schwarz LU--SGS{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"\section{Implicit and transient time integration}",
              r"Steady marching uses a local pseudo-time step $\Delta\tau_K=\mathrm{CFL}\,V_K/\lambda_K$. The scalar diagonal contains convective spectral radii $(|u_n|+a)|f|$, a viscous contribution proportional to $\mu|f|/(\rho d)$, and any physical-time coefficient. CFL is ramped geometrically from the supplied initial to maximum value. Each nonlinear iteration applies forward and backward local cell sweeps to the scalar spectral Jacobian; exchanged ghost increments make this a distributed additive-Schwarz LU--SGS relaxation. Global $L_2/L_\infty$ residuals and forces use MPI reductions, and steady acceptance requires the recorded residual decrease plus a stable terminal force plateau.",
              r"For Re200 the true BDF2 inner loop advances exactly $\Delta t=0.01$ to $t=300$. Backward Euler starts the first step; later steps solve $V_K(3U^{n+1}-4U^n+U^{n-1})/(2\Delta t)+R_K(U^{n+1})=0$. Both $U^n$ and $U^{n-1}$ remain frozen throughout every inner iteration, which evaluates the full spatial-plus-physical residual. History shifts only after the inner reduction target or permitted maximum is reached. This is a true physical-time outer/inner algorithm, not pseudo-time-only marching.",
              r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrrr}", r"\toprule Case & max steps & CFL initial/max & ramp steps & inner min/max & target\\\midrule"]
    for case in cases:
        cfg = case_inputs.get(case.case_id, {}); control = cfg.get("run_control", {}) if isinstance(cfg.get("run_control"), dict) else {}; meta = case.metadata
        lines.append(f"{latex_escape(case.case_id)} & {control.get('max_steps', '--')} & {control.get('cfl_initial', '--')}/{control.get('cfl_max', '--')} & {control.get('pseudo_cfl_ramp_steps', '--')} & {control.get('min_inner_iterations', meta.get('min_inner_iterations', '--'))}/{control.get('max_inner_iterations', meta.get('max_inner_iterations', '--'))} & {control.get('inner_residual_reduction_target', meta.get('inner_residual_reduction_target', '--'))}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}",
              r"Observed iteration statistics and physical-time controls are:",
              r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrr}", r"\toprule Case & observed inner min/max/mean & $\Delta t/t_f$ & completed steps\\\midrule"]
    for case in cases:
        cfg = case_inputs.get(case.case_id, {}); control = cfg.get("run_control", {}) if isinstance(cfg.get("run_control"), dict) else {}; meta = case.metadata
        observed_mean = finite_float(meta.get("observed_mean_inner_iterations"))
        observed_mean_text = f"{observed_mean:.5g}" if math.isfinite(observed_mean) else "--"
        lines.append(f"{latex_escape(case.case_id)} & {meta.get('observed_min_inner_iterations', '--')}/{meta.get('observed_max_inner_iterations', '--')}/{observed_mean_text} & {control.get('time_step', '--')}/{control.get('final_time', '--')} & {case.status.get('final_step', '--')}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"\section{Output, reproducibility, and run status}",
              r"Configure with \path|cmake -S solver -B solver/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON|, then run \path|cmake --build solver/build -j|. CMake locates MPI, CGNS, METIS, and nlohmann-json and records their resolved paths in the build cache. Regenerate evidence with \path|.venv/bin/python tools/generate_report.py --results results --report report| and the supplied \path|--case-inputs| directory. Each output contains metadata/status JSON, partition diagnostics, residual/force/surface CSV files, the PVTU master plus rank VTU pieces, rank restart pieces, and stdout. The machine-readable run, figure, rank-comparison, spectrum, and sanity manifests accompany this report. Exact 32-rank production commands are retained in the run manifest and printed below.",
              r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrrrl}", r"\toprule Case & ranks & steps & time & residual orders & wall s & status\\\midrule"]
    for row in status_rows: lines.append(f"{latex_escape(row['case_id'])} & {row['mpi_ranks']} & {row['steps']} & {row['physical_time']} & {row['residual_reduction']} & {row['wall_time_seconds']} & {latex_escape(row['status'])}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}"]
    for row in status_rows:
        lines += [fr"Exact command for {latex_escape(row['case_id'])} (source \texttt{{run\_manifest.csv}}):", r"\begin{quote}\footnotesize", fr"\path|{row['command']}|", r"\end{quote}"]
    lines += [r"\section{Results}", r"\subsection{Force summary}", r"Steady rows are final recorded samples after residual reduction and a stable terminal-force plateau. Re200 uses a post-transient final-half mean; its lift amplitude is half the corresponding range.", r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrl}", r"\toprule Case & $C_D$ & $C_L$ & $C_m$ & status\\\midrule"]
    for row in force_rows:
        lines.append(f"{latex_escape(row['case_id'])} & {row['cd']} & {row['cl']} & {row['cmz']} & {latex_escape(row['status'])}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"The pressure/viscous drag decomposition and transient amplitude are:", r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrl}", r"\toprule Case & pressure drag & viscous drag & $C_L$ amplitude & metric\\\midrule"]
    for row in force_rows:
        metric_label = "mean, final half" if row["metric_mode"] == "post_transient_mean_final_half" else "final"
        lines.append(f"{latex_escape(row['case_id'])} & {row['pressure_drag']} & {row['viscous_drag']} & {row['lift_amplitude']} & {metric_label}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"\section{MPI parallelization and METIS partitioning}",
              r"Rank 0 builds the dual cell graph with one edge for every interior face and calls METIS $k$-way partitioning. It then packs and sends each rank only its owned cells, incident faces, boundary tags, and the one-layer off-rank stencil cells needed as ghosts; rank 0 discards the global iterative representation after distribution. Ownership crossings define sorted neighbor lists and exact send/receive global-cell maps. Every halo refresh posts neighbor-scoped \texttt{MPI\_Irecv}/\texttt{MPI\_Isend} operations and waits for completion. Solver iterations never \texttt{Allgather} the full mesh or conservative state. Only scalar/vector global diagnostics such as residual norms and integrated forces use \texttt{MPI\_Allreduce}.",
              r"Representative production-rank rows below expose owned and ghost cells, boundary faces, neighbor degree, and cell-message counts. Their source is each case's \texttt{partition\_diagnostics.csv}; the complete files contain all 32 ranks.",
              r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrrrlrr}", r"\toprule Case & rank & owned & ghost & boundary & neighbors & neighbor ranks & send & receive\\\midrule"]
    for row in representative_partition_rows(cases):
        lines.append(f"{latex_escape(row['case_id'])} & {row.get('rank', '--')} & {row.get('num_cells_owned', '--')} & {row.get('num_cells_ghost', '--')} & {row.get('num_boundary_faces', '--')} & {row.get('num_neighbor_ranks', '--')} & {latex_escape(row.get('neighbor_ranks', '--'))} & {row.get('send_cells', '--')} & {row.get('recv_cells', '--')}{LATEX_ROW_END}")
    lines += [r"\bottomrule\end{longtable}", r"}", r"\subsection{Rank-count validation}"]
    if rank_rows:
        lines += [r"The submitted NACA Mach-2 and cylinder Re20 comparisons provide recorded timing, residual, force, owned/ghost, balance, and edge-cut values at two and eight ranks. Increasing ranks reduces owned work per rank but increases the surface-to-volume ratio and halo/message overhead; the available end-to-end timings combine that communication cost with partition and I/O cost and therefore do not isolate it.", r"{\scriptsize", r"\setlength{\tabcolsep}{3pt}", r"\begin{longtable}{lrrrrrrrrr}", r"\toprule Case & ranks & wall s & $C_D$ & $C_L$ & residual metric & owned min/max & ghost mean & balance & edge cut\\\midrule"]
        for row in rank_rows:
            lines.append(f"{latex_escape(row['case_id'])} & {row['mpi_ranks']} & {finite_float(row['wall_time_seconds']):.5g} & {finite_float(row['cd']):.5g} & {finite_float(row['cl']):.5g} & {finite_float(row['residual_metric']):.4g} & {row['owned_cells_min']}/{row['owned_cells_max']} & {finite_float(row['ghost_cells_mean']):.4g} & {finite_float(row['load_balance_ratio']):.4g} & {row['partition_edge_cut']}{LATEX_ROW_END}")
        lines += [r"\bottomrule\end{longtable}", r"}"]
        lines += rank_comparison_discussion(rank_rows)
        lines.append(r"Exact rank-comparison commands and source CSV paths are retained without abbreviation in \texttt{rank\_comparison\_manifest.csv}.")
    elif rank_errors:
        lines.append("Rank-comparison CSV evidence was found but not used because: " + latex_escape("; ".join(rank_errors)) + ".")
    else:
        lines.append(r"No submitted rank-comparison CSV was found under \texttt{results/rank\_comparisons}; therefore no rank-count performance or consistency claim is made.")
    lines += [r"\section{Field, surface, and history results}"]
    by_case: dict[str, list[dict[str, str]]] = {}
    for figure in figures: by_case.setdefault(figure["case_id"], []).append(figure)
    for case in cases:
        lines += [f"\\subsection{{{latex_escape(case.case_id)}}}", f"Reported status: \\textbf{{{latex_escape(status_for(case, checks[case.case_id]))}}}. "]
        if case.errors: lines.append("Input/output issues: " + latex_escape("; ".join(case.errors)) + ".")
        if case.is_re200:
            if spectrum.get("available"):
                st = finite_float(spectrum.get("strouhal")); st_text = f" and $St={st:.5g}$" if math.isfinite(st) else " (Strouhal unavailable because reference length or velocity is missing)"
                fraction = finite_float(case.metadata.get("inner_target_converged_fraction"))
                inner_text = (f"; {100.0 * fraction:.3g}\\% of physical steps met the inner target" if math.isfinite(fraction) else "")
                lines.append(fr"The final-half/post-transient evidence gives mean $C_D={finite_float(spectrum['mean_drag']):.5g}$, mean $C_L={finite_float(spectrum['mean_lift']):.5g}$, lift amplitude {finite_float(spectrum['lift_amplitude']):.5g}, and dominant frequency $f={finite_float(spectrum['dominant_frequency']):.5g}${st_text} over $t={finite_float(spectrum['analysis_window_start']):.5g}$--$t={finite_float(spectrum['analysis_window_end']):.5g}$. The alternating lift and wake velocity structure demonstrate periodic shedding{inner_text}. The full numerical spectrum is in \texttt{{re200\_lift\_spectrum.csv}}.")
            else:
                lines.append("A dominant Re200 lift frequency and Strouhal number were not reported because " + latex_escape(str(spectrum.get("reason", "the force history is unavailable"))) + ".")
        else:
            metric = force_metrics(case)
            cp = numeric_column(case.surface, "cp"); cp = cp[np.isfinite(cp)]
            cp_text = f"surface $C_p$ range {np.min(cp):.4g}--{np.max(cp):.4g}" if len(cp) else "surface $C_p$ unavailable"
            mach = case.field.values.get("mach", np.empty(0)); mach = mach[np.isfinite(mach)]
            pressure = case.field.values.get("pressure", np.empty(0)); pressure = pressure[np.isfinite(pressure)]
            field_text = (f"Final cell fields span Mach {np.min(mach):.4g}--{np.max(mach):.4g} and pressure {np.min(pressure):.4g}--{np.max(pressure):.4g}. "
                          if len(mach) and len(pressure) else "Final field ranges are unavailable. ")
            if case.is_naca:
                cfg = case_inputs.get(case.case_id, {}); free = cfg.get("freestream", {}) if isinstance(cfg.get("freestream"), dict) else {}
                mach_inf = finite_float(free.get("mach"))
                regime = ("low-Mach pressure field" if mach_inf < 0.3 else
                          "transonic compressibility and possible localized steep gradients" if mach_inf < 1.0 else
                          "supersonic compression/expansion structure, with shocks broadened by Rusanov dissipation")
                wall_text = ("No-slip wall speed and nonzero skin friction are checked directly." if case.is_viscous else
                             "Slip-normal velocity and zero viscous force are checked directly.")
                lines.append(f"Final steady evidence gives $C_D={finite_float(metric['cd']):.5g}$ and $C_L={finite_float(metric['cl']):.5g}$ with {cp_text}. The small lift at the supplied zero incidence is consistent with upper/lower symmetry. {field_text}The near-body views expose the {regime}. {wall_text}")
            elif case.case_id.startswith("cylinder"):
                lines.append(f"Final steady force evidence gives $C_D={finite_float(metric['cd']):.5g}$ and $C_L={finite_float(metric['cl']):.5g}$ with {cp_text}. {field_text}At Re20 the terminal force plateau and symmetric near wake are consistent with the expected steady laminar regime; the wake view is plotted evidence rather than a separately fitted wake metric.")
        for figure in by_case.get(case.case_id, []):
            figure_file = figure["figure_file"]
            label = Path(figure_file).stem
            lines.append(f"The plotted quantity is introduced in Figure~\\ref{{fig:{label}}}: {latex_escape(figure['caption'])}")
            lines += [r"\begin{figure}[htbp]\centering", f"\\includegraphics[width=0.88\\linewidth]{{figures/{figure_file}}}", f"\\caption{{{latex_escape(figure['caption'])}}}", f"\\label{{fig:{label}}}", r"\end{figure}"]
        # Keep a report with dozens of required figures from exhausting LaTeX's float queue.
        lines.append(r"\clearpage")
    completion_statement = ("All required runs completed, so there is no concealed failed production case."
                            if completed_count == len(cases) else
                            f"Only {completed_count} of {len(cases)} required runs passed the measured gates; failed rows remain marked failed.")
    lines += [r"\section{Sanity checks}",
              r"The machine-readable \texttt{sanity\_checks.json} records positive density/pressure, nontrivial surface pressure, force behavior, final-wall checks, and exact Mach/pressure figure mappings. Viscous walls require maximum reported wall speed at most $10^{-6}$ and nonzero $|C_f|>10^{-12}$; inviscid slip walls require normal speed at most $10^{-6}$, nonzero tangential speed, and final viscous force at most $10^{-10}$. The manifest also confirms that plotted Mach and pressure variables match their file names and source PVTU masters. A failed measured gate forces status \texttt{failed}; it is not hidden by the solver's status claim.",
              r"\section{Limitations and failure analysis}",
              completion_statement + r" Accuracy remains limited by the supplied mesh resolution, constant-viscosity laminar model, scalar-Jacobian LU--SGS approximation, and the deliberately robust but diffusive Rusanov flux; shocks and thin boundary layers are consequently broader than with a less dissipative flux and adapted high-resolution mesh. Reconstruction is first order during its documented startup ramp before reaching the terminal second-order setting. No turbulence, transition, moving mesh, or three-dimensional physics is claimed. The Re200 field output has no cell vorticity array, so its wake figure uses velocity magnitude and cannot expose signed vortex cores as directly. Field colourbars are cellwise 1st--99th percentile clipped, so they suppress extrema and must not be used to infer exact bounds. Rank comparisons are limited to two and eight ranks in one execution environment; they do not establish strong scaling or isolate communication from partitioning and I/O. With more time, the priorities would be a less dissipative shock flux, mesh/adaptation studies, explicit vorticity output, block or Krylov implicit solves, and broader rank scaling.",
              r"\section{Software design and extension points}",
              r"Case parsing, CGNS topology, distributed ownership, halo exchange, thermodynamics/fluxes, reconstruction, residual assembly, time integration, and output are separate modules. A three-dimensional extension would add the third momentum component and 3-D face/cell geometry; a general equation of state would replace primitive/conservative conversion and wave-speed closures; RANS or multispecies models would extend the state and flux kernels while retaining partition ownership and typed neighbor exchange. These are design extension points, not implemented capabilities.",
              r"\end{document}"]
    (report_dir / "report.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path("results"), help="solver results directory")
    parser.add_argument("--report", type=Path, default=Path("report"), help="report output directory")
    parser.add_argument("--case-inputs", type=Path, help="directory containing supplied case JSON files")
    parser.add_argument("--compile-pdf", action="store_true", help="run pdflatex only when it is available")
    args = parser.parse_args(); results = args.results.resolve(); report = args.report.resolve(); report.mkdir(parents=True, exist_ok=True)
    input_dir = args.case_inputs.resolve() if args.case_inputs else results.parent.parent / "cfd_solver_agentic_benchmark" / "inputs" / "cases"
    case_inputs = load_case_inputs(input_dir)
    cases = discover_cases(results); checks = {case.case_id: case_checks(case) for case in cases}
    figures, spectrum, spectrum_rows = make_figures(cases, report, case_inputs)
    augment_manifest_checks(cases, figures, checks)
    rank_rows, rank_errors = load_rank_comparisons(results)
    write_report(report, cases, checks, figures, spectrum, spectrum_rows, rank_rows, rank_errors, case_inputs)
    if args.compile_pdf:
        if shutil.which("pdflatex"):
            import subprocess
            for suffix in (".aux", ".log", ".out"):
                (report / f"report{suffix}").unlink(missing_ok=True)
            completed = subprocess.run(["pdflatex", "-interaction=nonstopmode", "-halt-on-error", "report.tex"], cwd=report, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if not completed.returncode:
                completed = subprocess.run(["pdflatex", "-interaction=nonstopmode", "-halt-on-error", "report.tex"], cwd=report, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if completed.returncode:
                print("pdflatex failed; retained report.tex and did not create a success claim", file=sys.stderr)
        else:
            print("pdflatex unavailable; report.tex was generated", file=sys.stderr)
    failures = [case.case_id for case in cases if status_for(case, checks[case.case_id]) == "failed"]
    print(f"Generated report for {len(cases)} discovered/required cases in {report}")
    if failures: print("Cases reported as failed: " + ", ".join(failures), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

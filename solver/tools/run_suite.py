#!/usr/bin/env python3
"""Run the eight benchmark cases and optional MPI rank comparisons safely."""

from __future__ import annotations

import argparse
import contextlib
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Iterable, Sequence
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


SOLVER_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = SOLVER_ROOT / "report" / "run_manifest.csv"
RESTART_NAME = "restart_checkpoint.bin"
RESTART_MAGIC = b"CFDRST15"
RESTART_VERSION = 15
SUPPORTED_RESTART_FORMATS = {
    b"CFDRST8\0": 8,
    b"CFDRST9\0": 9,
    b"CFDRST10": 10,
    b"CFDRST11": 11,
    b"CFDRST12": 12,
    b"CFDRST13": 13,
    b"CFDRST14": 14,
    RESTART_MAGIC: RESTART_VERSION,
}
RESTART_RECORD_BYTES = 8 + 16 * 8

EXPECTED_CASE_IDS = frozenset(
    {
        "naca0012_m015_inviscid",
        "naca0012_m080_inviscid",
        "naca0012_m200_inviscid",
        "naca0012_m015_laminar_re5000",
        "naca0012_m080_laminar_re5000",
        "naca0012_m200_laminar_re5000",
        "cylinder_m010_laminar_re20",
        "cylinder_m010_laminar_re200",
    }
)
DEFAULT_COMPARISON_CASES = (
    "naca0012_m015_inviscid",
    "cylinder_m010_laminar_re20",
)
DEFAULT_COMPARISON_RANKS = (2, 8)
SUCCESS_STATUSES = frozenset({"converged", "statistically_periodic"})

MANIFEST_FIELDS = [
    "case_id", "run_role", "case_file", "output_dir", "command", "mpi_ranks",
    "start_time_utc", "end_time_utc", "wall_time_seconds", "final_step",
    "final_physical_time", "convergence_status", "residual_reduction_orders",
    "git_revision", "executable_sha256", "exit_code",
]
LEGACY_MANIFEST_FIELDS = [field for field in MANIFEST_FIELDS if field != "executable_sha256"]

RESIDUAL_HEADER = [
    "step", "physical_time", "inner_iter", "cfl", "dt", "rho", "rhou", "rhov",
    "rhoE", "residual_l2", "residual_linf",
]
FORCES_HEADER = [
    "step", "physical_time", "cl", "cd", "cmz", "pressure_drag", "viscous_drag",
    "pressure_lift", "viscous_lift",
]
SURFACE_HEADER = [
    "x", "y", "nx", "ny", "pressure", "cp", "cf", "rho", "u", "v", "mach", "tag",
]
PARTITION_HEADER = [
    "rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
    "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells",
]
STATUS_FIELDS = {
    "case_id", "command", "mpi_ranks", "wall_time_seconds", "final_step",
    "final_physical_time", "convergence_status", "steady_initial_residual_scale",
    "steady_full_order_initial_residual", "residual_reduction_orders",
    "full_order_residual_reduction_orders", "notes",
}
REQUIRED_METADATA = {
    "case_id", "solver_name", "solver_version", "git_revision", "source_dirty",
    "executable_sha256", "mpi_ranks", "mesh_file", "num_cells_global",
    "num_faces_global", "num_cells_owned_local", "num_cells_ghost_local", "partitioner",
    "partition_edge_cut", "halo_exchange", "full_state_replication_during_iterations",
    "full_mesh_replication_during_iterations", "equation_set", "inviscid_flux",
    "entropy_fix", "viscous_flux", "time_integrator", "implicit_solver",
    "reconstruction", "limiter", "spatial_order_claimed", "positivity_preservation",
    "wall_boundary_output_semantics", "true_bdf2_inner_loop", "typical_inner_iterations",
    "min_inner_iterations", "max_inner_iterations", "observed_min_inner_iterations",
    "observed_max_inner_iterations", "inner_residual_reduction_target",
    "inner_target_misses", "inner_target_converged_fraction", "last_inner_residual_ratio",
    "start_time_utc", "end_time_utc", "completed", "convergence_status",
    "mesh_fingerprint", "case_config_fingerprint", "steady_initial_residual_scale",
    "steady_full_order_initial_residual", "residual_reduction_target",
    "residual_reduction_orders", "full_order_residual_reduction_orders",
    "steady_convergence_gate", "spatial_order_continuation",
}
REQUIRED_CELL_ARRAYS = {"rho", "u", "v", "pressure", "mach", "rhoE", "temperature", "owner_rank"}
HEX64 = re.compile(r"^[0-9a-fA-F]{64}$")
SHA256_FINGERPRINT = re.compile(r"^sha256:[0-9a-fA-F]{64}$")
FINGERPRINT = re.compile(r"^sha256:[0-9a-fA-F]{64}\|sha256:[0-9a-fA-F]{64}$")


class SuiteError(RuntimeError):
    """An actionable suite configuration or output error."""


@dataclass(frozen=True)
class RunSpec:
    case_id: str
    case_file: Path
    output_dir: Path
    ranks: int
    role: str


@dataclass(frozen=True)
class OutputSummary:
    metadata: dict
    status: dict


@dataclass(frozen=True)
class SuiteConfig:
    solver: Path
    mpirun: Path
    cases: dict[str, Path]
    results_dir: Path
    ranks: int
    comparisons_dir: Path | None
    comparison_ranks: tuple[int, ...]
    comparison_cases: tuple[str, ...]
    resume: bool = False
    dry_run: bool = False


@dataclass(frozen=True)
class RunOutcome:
    success: bool
    row: dict[str, str] | None
    skipped: bool = False


@dataclass(frozen=True)
class RestartInfo:
    cell_count: int
    step: int
    physical_time: float
    fingerprint: str
    history_complete: bool
    total_attempted_steps: int
    residual_output_rows: int
    force_output_rows: int
    last_residual_output_step: int | None
    last_force_output_step: int | None
    steady_initial_residual_scale: float
    steady_full_order_initial_residual: float
    steady_reconstruction_blend: float
    steady_full_order_accepted_steps: int
    steady_target_met: bool
    original_initial_residual_semantics: bool
    nonmonotone_bypass_attempts: int
    nonmonotone_bypass_accepted_steps: int
    nonmonotone_bypass_trial_evaluations: int
    nonmonotone_bypass_last_actual_trial_residual: float
    nonmonotone_bypass_last_gmres_ratio: float
    nonmonotone_envelope_reference: float
    nonmonotone_envelope_accepted_steps: int
    nonmonotone_envelope_max_relative_increase: float
    implicit_bridge_active: bool
    implicit_bridge_disabled: bool
    implicit_bridge_cfl: float
    implicit_bridge_entry_best_residual: float
    implicit_bridge_attempts: int
    implicit_bridge_accepted_steps: int
    implicit_bridge_rejected_steps: int
    implicit_bridge_steps_since_best: int
    implicit_bridge_residual_minimum: float
    implicit_bridge_residual_maximum: float
    implicit_bridge_maximum_relative_growth: float
    implicit_bridge_meaningful_best_improvements: int
    implicit_bridge_watchdog_stops: int
    implicit_bridge_linear_sweeps: int


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds").replace("+00:00", "Z")


def positive_int(value: str) -> int:
    try:
        result = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected an integer, got {value!r}") from exc
    if result <= 0:
        raise argparse.ArgumentTypeError("rank counts must be positive")
    return result


def discover_cases(cases_dir: Path) -> dict[str, Path]:
    if not cases_dir.is_dir():
        raise SuiteError(f"cases directory does not exist: {cases_dir}")
    files = sorted(cases_dir.glob("*.json"))
    found: dict[str, Path] = {}
    duplicates: list[str] = []
    errors: list[str] = []
    for path in files:
        try:
            value = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{path.name}: invalid JSON ({exc})")
            continue
        case_id = value.get("case_id") if isinstance(value, dict) else None
        if not isinstance(case_id, str) or not case_id.strip():
            errors.append(f"{path.name}: missing non-empty string case_id")
        elif case_id in found:
            duplicates.append(case_id)
        else:
            found[case_id] = path.resolve()
    missing = sorted(EXPECTED_CASE_IDS - found.keys())
    unexpected = sorted(found.keys() - EXPECTED_CASE_IDS)
    if len(files) != 8 or missing or unexpected or duplicates or errors:
        details = [f"expected exactly 8 JSON cases, found {len(files)}"]
        if missing:
            details.append("missing case_id(s): " + ", ".join(missing))
        if unexpected:
            details.append("unexpected case_id(s): " + ", ".join(unexpected))
        if duplicates:
            details.append("duplicate case_id(s): " + ", ".join(sorted(set(duplicates))))
        details.extend(errors)
        raise SuiteError("case discovery failed: " + "; ".join(details))
    return found


def resolve_executable(value: str | Path, label: str) -> Path:
    text = os.fspath(value)
    if Path(text).is_absolute() or Path(text).parent != Path("."):
        candidate = str(Path(text).expanduser().resolve())
    else:
        candidate = shutil.which(text)
    if not candidate or not Path(candidate).is_file() or not os.access(candidate, os.X_OK):
        raise SuiteError(f"{label} is not an executable file: {text}")
    return Path(candidate).resolve()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise SuiteError(f"cannot hash executable {path}: {exc}") from exc
    return digest.hexdigest()


def is_openmpi(mpirun: Path) -> bool:
    try:
        result = subprocess.run(
            [str(mpirun), "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=5, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    output = result.stdout.lower()
    return "open mpi" in output or "openrte" in output


def mpi_environment(mpirun: Path, ranks: int) -> tuple[dict[str, str], dict[str, str]]:
    env = os.environ.copy()
    changes: dict[str, str] = {}
    if is_openmpi(mpirun):
        if hasattr(os, "geteuid") and os.geteuid() == 0:
            changes["OMPI_ALLOW_RUN_AS_ROOT"] = "1"
            changes["OMPI_ALLOW_RUN_AS_ROOT_CONFIRM"] = "1"
        try:
            available = len(os.sched_getaffinity(0))
        except (AttributeError, OSError):
            available = os.cpu_count() or 1
        if ranks > available:
            changes["OMPI_MCA_rmaps_base_oversubscribe"] = "1"
    env.update(changes)
    return env, changes


def command_for(
    config: SuiteConfig,
    spec: RunSpec,
    output_dir: Path,
    restart: Path | None = None,
) -> list[str]:
    command = [
        str(config.mpirun), "-np", str(spec.ranks), str(config.solver), "solve",
        "--case", str(spec.case_file), "--output", str(output_dir), "--report-level", "full",
    ]
    if restart is not None:
        command.extend(["--restart", str(restart), "--resume-output"])
    return command


def quoted_command(argv: Sequence[str], env_changes: dict[str, str]) -> str:
    command = shlex.join(list(argv))
    if env_changes:
        assignments = [f"{key}={env_changes[key]}" for key in sorted(env_changes)]
        command = "env " + shlex.join(assignments) + " " + command
    return command


def read_json_object(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise SuiteError(f"missing {path.name}") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise SuiteError(f"cannot read {path.name}: {exc}") from exc
    if not isinstance(value, dict):
        raise SuiteError(f"{path.name} must contain a JSON object")
    return value


def finite_number(value: object, label: str, minimum: float | None = None) -> float:
    if isinstance(value, bool):
        raise SuiteError(f"{label} must be numeric")
    try:
        result = float(value)  # type: ignore[arg-type]
    except (TypeError, ValueError) as exc:
        raise SuiteError(f"{label} must be numeric") from exc
    if not math.isfinite(result) or (minimum is not None and result < minimum):
        constraint = "finite" if minimum is None else f"finite and >= {minimum}"
        raise SuiteError(f"{label} must be {constraint}")
    return result


def integer(value: object, label: str, minimum: int = 0) -> int:
    number = finite_number(value, label)
    if not number.is_integer() or number < minimum:
        raise SuiteError(f"{label} must be an integer >= {minimum}")
    return int(number)


def close(a: float, b: float, tolerance: float = 1.0e-10) -> bool:
    return math.isclose(a, b, rel_tol=tolerance, abs_tol=tolerance)


def parse_timestamp(value: object, label: str) -> datetime:
    if not isinstance(value, str) or not value:
        raise SuiteError(f"{label} must be a non-empty ISO-8601 string")
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise SuiteError(f"{label} is not valid ISO-8601") from exc


def read_csv_rows(
    path: Path,
    expected_header: list[str],
    string_fields: frozenset[str] = frozenset(),
) -> list[dict[str, float | str]]:
    rows: list[dict[str, float | str]] = []
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.reader(stream)
            header = next(reader, None)
            if header != expected_header:
                raise SuiteError(f"{path.name} header mismatch: {header!r}")
            for line_number, values in enumerate(reader, 2):
                if len(values) != len(expected_header):
                    raise SuiteError(
                        f"{path.name}:{line_number} has {len(values)} columns; expected {len(expected_header)}"
                    )
                row: dict[str, float | str] = {}
                for key, value in zip(expected_header, values):
                    if key in string_fields:
                        if not value.strip():
                            raise SuiteError(f"{path.name}:{line_number} {key} is empty")
                        row[key] = value
                    else:
                        row[key] = finite_number(value, f"{path.name}:{line_number} {key}")
                rows.append(row)
    except FileNotFoundError as exc:
        raise SuiteError(f"missing {path.name}") from exc
    except OSError as exc:
        raise SuiteError(f"cannot read {path.name}: {exc}") from exc
    if not rows:
        raise SuiteError(f"{path.name} has no data rows")
    return rows


def validate_histories(
    output_dir: Path,
    case_id: str,
    status: dict,
    case: dict,
) -> tuple[list[dict[str, float | str]], list[dict[str, float | str]]]:
    residuals = read_csv_rows(output_dir / "residuals.csv", RESIDUAL_HEADER)
    forces = read_csv_rows(output_dir / "forces.csv", FORCES_HEADER)
    surface = read_csv_rows(output_dir / "surface.csv", SURFACE_HEADER, frozenset({"tag"}))
    if len(residuals) < 2 or len(forces) < 2 or len(surface) < 3:
        raise SuiteError("CSV outputs are too short to be non-placeholder production histories")

    final_step = integer(status["final_step"], "run_status final_step", 1)
    final_time = finite_number(status["final_physical_time"], "run_status final_physical_time", 0.0)
    for name, rows in (("residuals.csv", residuals), ("forces.csv", forces)):
        steps = [integer(row["step"], f"{name} step") for row in rows]
        times = [float(row["physical_time"]) for row in rows]
        if steps != sorted(steps) or times != sorted(times):
            raise SuiteError(f"{name} step/time history is not monotonic")
        if max(steps) != final_step or steps[-1] != final_step:
            raise SuiteError(f"{name} last/max step does not equal run_status final_step")
        if not close(times[-1], final_time):
            raise SuiteError(f"{name} final physical_time does not match run_status")
        if len(set(steps)) < 2:
            raise SuiteError(f"{name} contains only one distinct step")

    for index, row in enumerate(residuals, 2):
        integer(row["inner_iter"], f"residuals.csv:{index} inner_iter", 0)
        if float(row["cfl"]) <= 0.0:
            raise SuiteError(f"residuals.csv:{index} cfl must be positive")
        for key in ("dt", "rho", "rhou", "rhov", "rhoE", "residual_l2", "residual_linf"):
            if float(row[key]) < 0.0:
                raise SuiteError(f"residuals.csv:{index} {key} must be nonnegative")
    for index, row in enumerate(surface, 2):
        if float(row["rho"]) <= 0.0 or float(row["pressure"]) <= 0.0 or float(row["mach"]) < 0.0:
            raise SuiteError(f"surface.csv:{index} contains nonphysical density/pressure/Mach")
    cp_values = [float(row["cp"]) for row in surface]
    coordinates = {(float(row["x"]), float(row["y"])) for row in surface}
    if len(coordinates) < 3 or max(cp_values) - min(cp_values) <= 1.0e-12:
        raise SuiteError("surface.csv appears to be placeholder data")

    physics = case.get("physics", {})
    if physics.get("mode") == "inviscid" or "inviscid" in case_id:
        for index, row in enumerate(forces, 2):
            if abs(float(row["viscous_drag"])) > 1.0e-8 or abs(float(row["viscous_lift"])) > 1.0e-8:
                raise SuiteError(f"forces.csv:{index} has nonzero inviscid viscous force")
        if any(abs(float(row["cf"])) > 1.0e-8 for row in surface):
            raise SuiteError("surface.csv has nonzero skin friction for an inviscid case")
    return residuals, forces


def parse_int_list(value: str, label: str) -> list[int]:
    if value == "":
        return []
    result: list[int] = []
    for item in value.split(";"):
        try:
            parsed = int(item)
        except ValueError as exc:
            raise SuiteError(f"{label} contains a non-integer entry") from exc
        if parsed < 0:
            raise SuiteError(f"{label} contains a negative entry")
        result.append(parsed)
    return result


def validate_partitions(output_dir: Path, metadata: dict, ranks: int) -> None:
    path = output_dir / "partition_diagnostics.csv"
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != PARTITION_HEADER:
                raise SuiteError(f"{path.name} header mismatch: {reader.fieldnames!r}")
            raw_rows = list(reader)
    except FileNotFoundError as exc:
        raise SuiteError("missing partition_diagnostics.csv") from exc
    except OSError as exc:
        raise SuiteError(f"cannot read partition diagnostics: {exc}") from exc
    if len(raw_rows) != ranks:
        raise SuiteError(f"partition diagnostics has {len(raw_rows)} rows; expected {ranks}")
    parsed: dict[int, tuple[int, int, list[int], list[int], list[int]]] = {}
    for line, row in enumerate(raw_rows, 2):
        rank = integer(row["rank"], f"partition:{line} rank")
        owned = integer(row["num_cells_owned"], f"partition:{line} owned", 1)
        ghost = integer(row["num_cells_ghost"], f"partition:{line} ghost")
        integer(row["num_boundary_faces"], f"partition:{line} boundary faces")
        neighbors_count = integer(row["num_neighbor_ranks"], f"partition:{line} neighbor count")
        neighbors = parse_int_list(row["neighbor_ranks"], f"partition:{line} neighbor_ranks")
        sends = parse_int_list(row["send_cells"], f"partition:{line} send_cells")
        receives = parse_int_list(row["recv_cells"], f"partition:{line} recv_cells")
        if rank in parsed or rank >= ranks:
            raise SuiteError(f"partition diagnostics has invalid/duplicate rank {rank}")
        if len(neighbors) != neighbors_count or len(sends) != neighbors_count or len(receives) != neighbors_count:
            raise SuiteError(f"partition:{line} neighbor/count list lengths disagree")
        if rank in neighbors or len(set(neighbors)) != len(neighbors) or any(peer >= ranks for peer in neighbors):
            raise SuiteError(f"partition:{line} has invalid neighbor ranks")
        parsed[rank] = (owned, ghost, neighbors, sends, receives)
    if set(parsed) != set(range(ranks)):
        raise SuiteError("partition ranks are not contiguous [0, mpi_ranks)")
    if sum(value[0] for value in parsed.values()) != integer(metadata["num_cells_global"], "metadata num_cells_global", 1):
        raise SuiteError("partition owned-cell sum does not match num_cells_global")
    if parsed[0][0] != integer(metadata["num_cells_owned_local"], "metadata num_cells_owned_local", 1):
        raise SuiteError("rank-0 owned count disagrees with metadata")
    if parsed[0][1] != integer(metadata["num_cells_ghost_local"], "metadata num_cells_ghost_local"):
        raise SuiteError("rank-0 ghost count disagrees with metadata")
    for rank, (_, _, neighbors, sends, receives) in parsed.items():
        for index, peer in enumerate(neighbors):
            peer_neighbors = parsed[peer][2]
            if rank not in peer_neighbors:
                raise SuiteError(f"partition neighbor relation {rank}<->{peer} is not reciprocal")
            peer_index = peer_neighbors.index(rank)
            if sends[index] != parsed[peer][4][peer_index] or receives[index] != parsed[peer][3][peer_index]:
                raise SuiteError(f"partition send/receive counts disagree for ranks {rank} and {peer}")


def local_name(element: ET.Element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def named_arrays(parent: ET.Element) -> dict[str, ET.Element]:
    return {
        child.attrib["Name"]: child
        for child in parent
        if local_name(child) == "DataArray" and "Name" in child.attrib
    }


def array_values(element: ET.Element, label: str, integer_values: bool = False) -> list[float | int]:
    tokens = (element.text or "").split()
    if not tokens:
        raise SuiteError(f"VTU {label} array is empty")
    if integer_values:
        return [integer(token, f"VTU {label}") for token in tokens]
    return [finite_number(token, f"VTU {label}") for token in tokens]


def validate_vtu(output_dir: Path, metadata: dict, status: dict, ranks: int) -> None:
    path = output_dir / "field_final.vtu"
    if not path.is_file() or path.stat().st_size < 512:
        raise SuiteError("field_final.vtu is missing, truncated, or placeholder-sized")
    try:
        root = ET.parse(path).getroot()
    except (ET.ParseError, OSError) as exc:
        raise SuiteError(f"field_final.vtu is invalid XML: {exc}") from exc
    if local_name(root) != "VTKFile" or root.attrib.get("type") != "UnstructuredGrid":
        raise SuiteError("field_final.vtu is not an UnstructuredGrid VTKFile")
    pieces = [element for element in root.iter() if local_name(element) == "Piece"]
    if len(pieces) != 1:
        raise SuiteError("field_final.vtu must contain exactly one Piece")
    piece = pieces[0]
    cells = integer(piece.attrib.get("NumberOfCells"), "VTU NumberOfCells", 1)
    points = integer(piece.attrib.get("NumberOfPoints"), "VTU NumberOfPoints", 3)
    if cells != integer(metadata["num_cells_global"], "metadata num_cells_global", 1):
        raise SuiteError("VTU Piece cell count disagrees with metadata")

    cell_data = next((child for child in piece if local_name(child) == "CellData"), None)
    if cell_data is None:
        raise SuiteError("VTU is missing CellData")
    arrays = named_arrays(cell_data)
    missing = sorted(REQUIRED_CELL_ARRAYS - arrays.keys())
    if missing:
        raise SuiteError("VTU CellData missing array(s): " + ", ".join(missing))
    values_by_name: dict[str, list[float | int]] = {}
    for name in REQUIRED_CELL_ARRAYS:
        values = array_values(arrays[name], name, name == "owner_rank")
        if len(values) != cells:
            raise SuiteError(f"VTU {name} has {len(values)} values; expected {cells}")
        values_by_name[name] = values
    if any(float(value) <= 0.0 for value in values_by_name["rho"] + values_by_name["pressure"]):
        raise SuiteError("VTU contains nonpositive density or pressure")
    if any(float(value) < 0.0 for value in values_by_name["mach"]):
        raise SuiteError("VTU contains negative Mach")
    if any(int(value) >= ranks for value in values_by_name["owner_rank"]):
        raise SuiteError("VTU owner_rank is outside MPI rank range")
    pressure = [float(value) for value in values_by_name["pressure"]]
    if max(pressure) - min(pressure) <= 1.0e-12:
        raise SuiteError("VTU pressure field appears to be placeholder data")

    points_node = next((child for child in piece if local_name(child) == "Points"), None)
    if points_node is None:
        raise SuiteError("VTU is missing Points")
    point_arrays = [child for child in points_node if local_name(child) == "DataArray"]
    if len(point_arrays) != 1 or len(array_values(point_arrays[0], "points")) != points * 3:
        raise SuiteError("VTU point coordinate count is invalid")
    cells_node = next((child for child in piece if local_name(child) == "Cells"), None)
    if cells_node is None:
        raise SuiteError("VTU is missing Cells")
    topology = named_arrays(cells_node)
    if not {"connectivity", "offsets", "types"}.issubset(topology):
        raise SuiteError("VTU Cells is missing connectivity/offsets/types")
    connectivity = array_values(topology["connectivity"], "connectivity", True)
    offsets = array_values(topology["offsets"], "offsets", True)
    types = array_values(topology["types"], "types", True)
    if len(offsets) != cells or len(types) != cells or int(offsets[-1]) != len(connectivity):
        raise SuiteError("VTU cell topology lengths are inconsistent")
    if any(int(index) >= points for index in connectivity):
        raise SuiteError("VTU connectivity references an invalid point")

    field_data = next((child for child in piece if local_name(child) == "FieldData"), None)
    if field_data is None:
        field_data = next((element for element in root.iter() if local_name(element) == "FieldData"), None)
    if field_data is None:
        raise SuiteError("VTU is missing FieldData")
    fields = named_arrays(field_data)
    if not {"step", "time"}.issubset(fields):
        raise SuiteError("VTU FieldData is missing step/time")
    if integer(array_values(fields["step"], "step")[0], "VTU step") != integer(status["final_step"], "status final_step"):
        raise SuiteError("VTU step does not match run_status")
    if not close(float(array_values(fields["time"], "time")[0]), float(status["final_physical_time"])):
        raise SuiteError("VTU time does not match run_status")


def read_exact(stream: object, count: int, label: str) -> bytes:
    data = stream.read(count)  # type: ignore[attr-defined]
    if len(data) != count:
        raise SuiteError(f"truncated restart {label}")
    return data


class ContinuationReader:
    def __init__(self, data: bytes):
        self.data = data
        self.position = 0

    def unpack(self, format_string: str, label: str) -> Any:
        size = struct.calcsize(format_string)
        if self.position + size > len(self.data):
            raise SuiteError(f"truncated restart continuation {label}")
        result = struct.unpack_from(format_string, self.data, self.position)[0]
        self.position += size
        return result

    def unsigned(self, label: str) -> int:
        return int(self.unpack("<Q", label))

    def signed(self, label: str) -> int:
        return int(self.unpack("<i", label))

    def number(self, label: str) -> float:
        value = float(self.unpack("<d", label))
        if not math.isfinite(value):
            raise SuiteError(f"restart continuation {label} must be finite")
        return value

    def boolean(self, label: str) -> bool:
        value = int(self.unpack("<B", label))
        if value not in (0, 1):
            raise SuiteError(f"restart continuation {label} must be encoded as 0 or 1")
        return bool(value)

    def string(self, label: str) -> str:
        size = self.unsigned(f"{label} length")
        if size > 4096 or self.position + size > len(self.data):
            raise SuiteError(f"restart continuation {label} is oversized or truncated")
        raw = self.data[self.position : self.position + size]
        self.position += size
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise SuiteError(f"restart continuation {label} is not valid UTF-8") from exc


def validate_continuation(
    data: bytes, restart_step: int, restart_version: int
) -> dict[str, Any]:
    reader = ContinuationReader(data)
    reader.number("steady_residual_baseline")
    reader.number("last_residual")
    inner_samples = reader.unsigned("inner samples")
    inner_minimum = reader.signed("inner minimum")
    inner_maximum = reader.signed("inner maximum")
    inner_mean = reader.number("inner mean")
    inner_misses = reader.unsigned("inner target misses")
    inner_fraction = reader.number("inner target fraction")
    inner_last_ratio = reader.number("inner last ratio")
    start_time = reader.string("original start time")
    reader.boolean("all accepted transient targets")
    history_complete = reader.boolean("history complete")
    reader.unsigned("rollbacks")
    total_attempted_steps = reader.unsigned("total attempted steps")
    last_inner_iterations = reader.signed("last inner iterations")
    residual_rows = reader.unsigned("residual output rows")
    force_rows = reader.unsigned("force output rows")
    has_residual_step = reader.boolean("has last residual step")
    residual_step_value = reader.unsigned("last residual step")
    has_force_step = reader.boolean("has last force step")
    force_step_value = reader.unsigned("last force step")
    cfl = reader.number("solver CFL")
    solver_nonlinear_steps = reader.unsigned("solver nonlinear steps")
    steady_previous_residual = reader.number("solver previous steady residual")
    steady_best_residual = reader.number("solver best steady residual")
    steady_trend_reference = reader.number("solver steady trend reference")
    steady_trend_samples = reader.signed("solver steady trend samples")
    reader.boolean("solver steady probe active")
    steady_rejected_attempts = reader.signed("solver rejected steady attempts")
    steady_recovery_probe_cfl = reader.number("solver recovery probe CFL")
    recovery_restore_pending = reader.boolean("solver recovery restore pending")
    steady_jfnk_accepted_steps = reader.unsigned("solver JFNK accepted steps")
    steady_fallback_accepted_steps = reader.unsigned("solver fallback accepted steps")
    steady_fallback_attempts = reader.unsigned("solver fallback attempts")
    steady_fallback_rejected_steps = reader.unsigned("solver fallback rejected steps")
    reader.unsigned("solver fallback CFL halvings")
    steady_last_fallback_cfl = reader.number("solver last fallback CFL")
    steady_fallback_mode = reader.boolean("solver fallback mode")
    steady_fallback_cfl = reader.number("solver fallback CFL")
    steady_fallback_steps_since_jfnk = reader.unsigned(
        "solver fallback steps since JFNK"
    )
    steady_jfnk_failure_streak = reader.signed("solver JFNK failure streak")
    steady_jfnk_attempts = reader.unsigned("solver JFNK attempts")
    steady_initial_residual_scale = reader.number(
        "solver initial residual scale"
    )
    steady_reconstruction_blend = reader.number(
        "solver reconstruction blend"
    )
    steady_first_order_steps = reader.unsigned(
        "solver first-order accepted steps"
    )
    steady_ramp_steps = reader.unsigned("solver order-ramp accepted steps")
    steady_full_order_steps = reader.unsigned(
        "solver full-order accepted steps"
    )
    steady_full_order_initial = reader.number(
        "solver full-order initial residual"
    )
    steady_full_order_best = reader.number(
        "solver full-order best residual"
    )
    steady_order_rescue_promoted = reader.boolean(
        "solver order promoted by rescue"
    )
    steady_rescue_attempts = reader.unsigned("solver rescue attempts")
    steady_rescue_accepted_steps = reader.unsigned(
        "solver rescue accepted steps"
    )
    steady_rescue_total_gmres_iterations = reader.unsigned(
        "solver rescue total GMRES iterations"
    )
    steady_rescue_last_gmres_iterations = reader.signed(
        "solver rescue last GMRES iterations"
    )
    steady_rescue_max_gmres_iterations = reader.signed(
        "solver rescue maximum GMRES iterations"
    )
    steady_rescue_last_gmres_ratio = reader.number(
        "solver rescue last GMRES ratio"
    )
    steady_rescue_last_cfl = reader.number("solver rescue last CFL")
    steady_rescue_last_line_scale = reader.number(
        "solver rescue last line scale"
    )
    steady_rescue_reference_residual = reader.number(
        "solver rescue reference residual"
    )
    steady_rescue_stagnation_count = reader.unsigned(
        "solver rescue stagnation count"
    )
    steady_rescue_cooldown_attempts = reader.unsigned(
        "solver rescue cooldown attempts"
    )
    steady_fallback_disabled = reader.boolean("solver fallback disabled")
    steady_fallback_consecutive_steps = reader.unsigned(
        "solver consecutive fallback accepted steps"
    )
    reader.unsigned("solver fallback growth disables")
    lusgs_applications = reader.unsigned("solver LU-SGS preconditioner applications")
    lusgs_sweeps = reader.unsigned("solver LU-SGS preconditioner sweeps")
    lusgs_last_defect_ratio = reader.number("solver LU-SGS last defect ratio")
    fallback_window_size = reader.unsigned("solver fallback residual window size")
    if fallback_window_size > 32:
        raise SuiteError("restart continuation fallback residual window exceeds 32")
    fallback_window = [
        reader.number(f"solver fallback residual window[{index}]")
        for index in range(fallback_window_size)
    ]
    transient_minimum = reader.signed("transient minimum")
    transient_mean = reader.number("transient mean")
    transient_maximum = reader.signed("transient maximum")
    transient_misses = reader.unsigned("transient target misses")
    transient_fraction = reader.number("transient target fraction")
    transient_last_ratio = reader.number("transient last ratio")
    transient_samples = reader.unsigned("transient samples")
    transient_iteration_sum = reader.number("transient iteration sum")
    steady_target_met = reader.boolean("steady target met")
    force_window_size = reader.unsigned("force window size")
    if force_window_size > 4096:
        raise SuiteError("restart continuation force_window exceeds 4096 entries")
    for index in range(force_window_size):
        reader.number(f"force_window[{index}].cl")
        reader.number(f"force_window[{index}].cd")
    trust_retry_batches = 0
    trust_retry_candidates = 0
    trust_retry_accepted_steps = 0
    trust_retry_total_gmres_iterations = 0
    trust_retry_last_candidate_count = 0
    trust_retry_last_total_gmres_iterations = 0
    trust_retry_last_accepted_gmres_iterations = 0
    trust_retry_last_accepted_cfl = 0.0
    trust_retry_last_line_scale = 0.0
    trust_retry_last_initial_residual = -1.0
    trust_retry_last_final_residual = -1.0
    trust_retry_cooldown_attempts = 0
    if restart_version >= 9:
        trust_retry_batches = reader.unsigned("solver trust retry batches")
        trust_retry_candidates = reader.unsigned("solver trust retry candidates")
        trust_retry_accepted_steps = reader.unsigned(
            "solver trust retry accepted steps"
        )
        trust_retry_total_gmres_iterations = reader.unsigned(
            "solver trust retry total GMRES iterations"
        )
        trust_retry_last_candidate_count = reader.unsigned(
            "solver trust retry last candidate count"
        )
        trust_retry_last_total_gmres_iterations = reader.signed(
            "solver trust retry last total GMRES iterations"
        )
        trust_retry_last_accepted_gmres_iterations = reader.signed(
            "solver trust retry last accepted GMRES iterations"
        )
        trust_retry_last_accepted_cfl = reader.number(
            "solver trust retry last accepted CFL"
        )
        trust_retry_last_line_scale = reader.number(
            "solver trust retry last line scale"
        )
        trust_retry_last_initial_residual = reader.number(
            "solver trust retry last initial residual"
        )
        trust_retry_last_final_residual = reader.number(
            "solver trust retry last final residual"
        )
        trust_retry_cooldown_attempts = reader.unsigned(
            "solver trust retry cooldown attempts"
        )
    jfnk_epsilon_reference = -1.0
    jfnk_epsilon_multiplier = 1.0
    jfnk_last_epsilon = 0.0
    jfnk_last_epsilon_halvings = 0
    if restart_version >= 10:
        jfnk_epsilon_reference = reader.number(
            "solver JFNK epsilon reference residual"
        )
        jfnk_epsilon_multiplier = reader.number(
            "solver JFNK epsilon multiplier"
        )
        jfnk_last_epsilon = reader.number("solver JFNK last epsilon")
        jfnk_last_epsilon_halvings = reader.signed(
            "solver JFNK last epsilon halvings"
        )
    original_initial_residual_semantics = False
    if restart_version >= 11:
        original_initial_residual_semantics = reader.boolean(
            "solver initial residual has original-run semantics"
        )
    nonmonotone_window: list[float] = []
    strict_decrease_stagnation_streak = 0
    nonmonotone_bridge_active = False
    nonmonotone_bridge_disabled = False
    nonmonotone_steps_since_strict_best = 0
    nonmonotone_accepted_steps = 0
    nonmonotone_max_relative_increase = 0.0
    nonmonotone_strict_best_improvements = 0
    nonmonotone_watchdog_resets = 0
    if restart_version >= 12:
        nonmonotone_window_size = reader.unsigned(
            "solver nonmonotone residual window size"
        )
        if nonmonotone_window_size > 10:
            raise SuiteError(
                "restart continuation nonmonotone residual window exceeds 10"
            )
        nonmonotone_window = [
            reader.number(f"solver nonmonotone residual window[{index}]")
            for index in range(nonmonotone_window_size)
        ]
        strict_decrease_stagnation_streak = reader.unsigned(
            "solver strict-decrease stagnation streak"
        )
        nonmonotone_bridge_active = reader.boolean(
            "solver nonmonotone bridge active"
        )
        nonmonotone_bridge_disabled = reader.boolean(
            "solver nonmonotone bridge disabled"
        )
        nonmonotone_steps_since_strict_best = reader.unsigned(
            "solver nonmonotone steps since strict best"
        )
        nonmonotone_accepted_steps = reader.unsigned(
            "solver nonmonotone accepted steps"
        )
        nonmonotone_max_relative_increase = reader.number(
            "solver nonmonotone maximum relative increase"
        )
        nonmonotone_strict_best_improvements = reader.unsigned(
            "solver nonmonotone strict-best improvements"
        )
        nonmonotone_watchdog_resets = reader.unsigned(
            "solver nonmonotone watchdog resets"
        )
    nonmonotone_bypass_attempts = 0
    nonmonotone_bypass_accepted_steps = 0
    nonmonotone_bypass_trial_evaluations = 0
    nonmonotone_bypass_last_actual_trial_residual = -1.0
    nonmonotone_bypass_last_gmres_ratio = 1.0
    if restart_version >= 13:
        nonmonotone_bypass_attempts = reader.unsigned(
            "solver nonmonotone descent-bypass attempts"
        )
        nonmonotone_bypass_accepted_steps = reader.unsigned(
            "solver nonmonotone descent-bypass accepted steps"
        )
        nonmonotone_bypass_trial_evaluations = reader.unsigned(
            "solver nonmonotone descent-bypass trial evaluations"
        )
        nonmonotone_bypass_last_actual_trial_residual = reader.number(
            "solver nonmonotone descent-bypass last actual trial residual"
        )
        nonmonotone_bypass_last_gmres_ratio = reader.number(
            "solver nonmonotone descent-bypass last GMRES ratio"
        )
    nonmonotone_envelope_reference = -1.0
    nonmonotone_envelope_accepted_steps = 0
    nonmonotone_envelope_max_relative_increase = 0.0
    if restart_version >= 14:
        nonmonotone_envelope_reference = reader.number(
            "solver nonmonotone activation-envelope reference"
        )
        nonmonotone_envelope_accepted_steps = reader.unsigned(
            "solver nonmonotone activation-envelope accepted steps"
        )
        nonmonotone_envelope_max_relative_increase = reader.number(
            "solver nonmonotone activation-envelope maximum relative increase"
        )
    implicit_bridge_active = False
    implicit_bridge_disabled = False
    implicit_bridge_cfl = 0.1
    implicit_bridge_entry_best_residual = -1.0
    implicit_bridge_attempts = 0
    implicit_bridge_accepted_steps = 0
    implicit_bridge_rejected_steps = 0
    implicit_bridge_steps_since_best = 0
    implicit_bridge_residual_minimum = -1.0
    implicit_bridge_residual_maximum = -1.0
    implicit_bridge_maximum_relative_growth = 0.0
    implicit_bridge_meaningful_best_improvements = 0
    implicit_bridge_watchdog_stops = 0
    implicit_bridge_linear_sweeps = 0
    if restart_version >= 15:
        implicit_bridge_active = reader.boolean("solver implicit bridge active")
        implicit_bridge_disabled = reader.boolean("solver implicit bridge disabled")
        implicit_bridge_cfl = reader.number("solver implicit bridge CFL")
        implicit_bridge_entry_best_residual = reader.number(
            "solver implicit bridge entry-best residual"
        )
        implicit_bridge_attempts = reader.unsigned("solver implicit bridge attempts")
        implicit_bridge_accepted_steps = reader.unsigned(
            "solver implicit bridge accepted steps"
        )
        implicit_bridge_rejected_steps = reader.unsigned(
            "solver implicit bridge rejected steps"
        )
        implicit_bridge_steps_since_best = reader.unsigned(
            "solver implicit bridge steps since best"
        )
        implicit_bridge_residual_minimum = reader.number(
            "solver implicit bridge residual minimum"
        )
        implicit_bridge_residual_maximum = reader.number(
            "solver implicit bridge residual maximum"
        )
        implicit_bridge_maximum_relative_growth = reader.number(
            "solver implicit bridge maximum relative growth"
        )
        implicit_bridge_meaningful_best_improvements = reader.unsigned(
            "solver implicit bridge meaningful-best improvements"
        )
        implicit_bridge_watchdog_stops = reader.unsigned(
            "solver implicit bridge watchdog stops"
        )
        implicit_bridge_linear_sweeps = reader.unsigned(
            "solver implicit bridge linear sweeps"
        )
    if reader.position != len(data):
        raise SuiteError("restart continuation has trailing bytes")

    if inner_minimum < 0 or inner_maximum < inner_minimum or last_inner_iterations < 0:
        raise SuiteError("restart continuation inner iteration bounds are invalid")
    if inner_misses > inner_samples or not 0.0 <= inner_fraction <= 1.0 or inner_last_ratio < 0.0:
        raise SuiteError("restart continuation inner statistics are invalid")
    if inner_samples and not inner_minimum <= inner_mean <= inner_maximum:
        raise SuiteError("restart continuation inner mean is outside min/max")
    if transient_minimum < 0 or transient_maximum < transient_minimum:
        raise SuiteError("restart continuation transient iteration bounds are invalid")
    if transient_misses > transient_samples or not 0.0 <= transient_fraction <= 1.0:
        raise SuiteError("restart continuation transient fraction/counters are invalid")
    if transient_last_ratio < 0.0 or transient_iteration_sum < 0.0 or cfl < 0.0:
        raise SuiteError("restart continuation transient ratio/sum/CFL is invalid")
    if (
        steady_previous_residual < -1.0
        or steady_best_residual < -1.0
        or (steady_previous_residual < 0.0) != (steady_best_residual < 0.0)
        or (steady_best_residual >= 0.0 and steady_best_residual > steady_previous_residual)
        or steady_trend_reference < -1.0
        or not 0 <= steady_trend_samples <= 3
        or (steady_trend_reference < 0.0 and steady_trend_samples != 0)
        or steady_rejected_attempts < 0
        or steady_recovery_probe_cfl != 0.0
        or recovery_restore_pending
        or steady_fallback_accepted_steps > steady_fallback_attempts
        or steady_fallback_rejected_steps
        != steady_fallback_attempts - steady_fallback_accepted_steps
        or steady_jfnk_accepted_steps > steady_jfnk_attempts
        or steady_rescue_accepted_steps > steady_rescue_attempts
        or steady_jfnk_accepted_steps
        + steady_fallback_accepted_steps
        + steady_rescue_accepted_steps
        + trust_retry_accepted_steps
        > solver_nonlinear_steps
        or not 0 <= steady_rescue_last_gmres_iterations <= 50
        or not steady_rescue_last_gmres_iterations
        <= steady_rescue_max_gmres_iterations
        <= 50
        or steady_rescue_last_gmres_ratio < 0.0
        or steady_rescue_last_cfl < 0.0
        or not 0.0 <= steady_rescue_last_line_scale <= 1.0
        or steady_rescue_reference_residual < -1.0
        or (
            steady_rescue_reference_residual < 0.0
            and steady_rescue_stagnation_count != 0
        )
        or steady_rescue_cooldown_attempts > 32
        or trust_retry_accepted_steps > trust_retry_batches
        or trust_retry_candidates < trust_retry_batches
        or trust_retry_last_candidate_count > trust_retry_candidates
        or trust_retry_last_total_gmres_iterations < 0
        or trust_retry_last_accepted_gmres_iterations < 0
        or trust_retry_last_total_gmres_iterations
        > trust_retry_total_gmres_iterations
        or trust_retry_last_accepted_gmres_iterations > 50
        or trust_retry_last_accepted_cfl < 0.0
        or not 0.0 <= trust_retry_last_line_scale <= 1.0
        or trust_retry_last_initial_residual < -1.0
        or trust_retry_last_final_residual < -1.0
        or trust_retry_cooldown_attempts > 16
        or (
            trust_retry_batches == 0
            and (
                trust_retry_candidates != 0
                or trust_retry_accepted_steps != 0
                or trust_retry_total_gmres_iterations != 0
                or trust_retry_last_candidate_count != 0
                or trust_retry_last_total_gmres_iterations != 0
                or trust_retry_last_accepted_gmres_iterations != 0
                or trust_retry_last_accepted_cfl != 0.0
                or trust_retry_last_line_scale != 0.0
                or trust_retry_last_initial_residual != -1.0
                or trust_retry_last_final_residual != -1.0
                or trust_retry_cooldown_attempts != 0
            )
        )
        or (
            trust_retry_batches > 0
            and trust_retry_last_candidate_count == 0
        )
        or (
            trust_retry_accepted_steps == 0
            and (
                trust_retry_last_accepted_cfl != 0.0
                or trust_retry_last_line_scale != 0.0
                or trust_retry_last_accepted_gmres_iterations != 0
                or trust_retry_last_initial_residual != -1.0
                or trust_retry_last_final_residual != -1.0
            )
        )
        or (
            trust_retry_accepted_steps > 0
            and trust_retry_last_accepted_cfl > 0.0
            and (
                trust_retry_last_final_residual
                >= trust_retry_last_initial_residual
                or trust_retry_last_initial_residual < 0.0
                or trust_retry_last_line_scale <= 0.0
                or trust_retry_last_accepted_gmres_iterations <= 0
            )
        )
        or (
            trust_retry_accepted_steps > 0
            and trust_retry_last_accepted_cfl == 0.0
            and (
                trust_retry_last_line_scale != 0.0
                or trust_retry_last_accepted_gmres_iterations != 0
                or trust_retry_last_initial_residual < 0.0
                or trust_retry_last_final_residual
                != trust_retry_last_initial_residual
            )
        )
        or (
            steady_rescue_attempts == 0
            and (
                steady_rescue_accepted_steps != 0
                or steady_rescue_total_gmres_iterations != 0
                or steady_rescue_last_gmres_iterations != 0
                or steady_rescue_max_gmres_iterations != 0
                or steady_rescue_last_cfl != 0.0
                or steady_rescue_last_line_scale != 0.0
            )
        )
        or (steady_rescue_attempts > 0 and steady_rescue_last_cfl <= 0.0)
        or steady_fallback_consecutive_steps > 64
        or steady_fallback_consecutive_steps > steady_fallback_accepted_steps
        or (steady_fallback_disabled and steady_fallback_mode)
        or steady_order_rescue_promoted
        or not 1.0e-6 <= steady_fallback_cfl <= 0.1
        or not 0.0 <= steady_last_fallback_cfl <= 0.1
        or steady_fallback_steps_since_jfnk > 32
        or not 0 <= steady_jfnk_failure_streak <= 2
        or lusgs_sweeps < 3 * lusgs_applications
        or lusgs_last_defect_ratio < 0.0
        or steady_initial_residual_scale < -1.0
        or jfnk_epsilon_reference < -1.0
        or not 1.0e-3 <= jfnk_epsilon_multiplier <= 1.0
        or jfnk_last_epsilon < 0.0
        or not 0 <= jfnk_last_epsilon_halvings <= 30
        or (
            jfnk_epsilon_reference < 0.0
            and (
                jfnk_epsilon_multiplier != 1.0
                or jfnk_last_epsilon != 0.0
                or jfnk_last_epsilon_halvings != 0
            )
        )
        or not 0.0 <= steady_reconstruction_blend <= 1.0
        or steady_first_order_steps > solver_nonlinear_steps
        or steady_ramp_steps > solver_nonlinear_steps
        or steady_full_order_steps > solver_nonlinear_steps
        or (steady_full_order_initial < 0.0)
        != (steady_full_order_best < 0.0)
        or (steady_full_order_steps == 0)
        != (steady_full_order_initial < 0.0)
        or (
            steady_full_order_best >= 0.0
            and steady_full_order_best > steady_full_order_initial
        )
        or (
            steady_target_met
            and (
                steady_reconstruction_blend != 1.0
                or steady_full_order_steps < (50 if restart_version >= 11 else 32)
            )
        )
        or any(residual < 0.0 for residual in fallback_window)
        or any(residual < 0.0 for residual in nonmonotone_window)
        or strict_decrease_stagnation_streak > 3
        or (nonmonotone_bridge_active and nonmonotone_bridge_disabled)
        or nonmonotone_steps_since_strict_best >= 20
        or nonmonotone_accepted_steps > solver_nonlinear_steps
        or not 0.0 <= nonmonotone_max_relative_increase <= 0.010000000001
        or nonmonotone_bypass_accepted_steps > nonmonotone_bypass_attempts
        or nonmonotone_bypass_accepted_steps > nonmonotone_accepted_steps
        or nonmonotone_bypass_last_actual_trial_residual < -1.0
        or nonmonotone_bypass_last_gmres_ratio < 0.0
        or nonmonotone_envelope_reference < -1.0
        or nonmonotone_envelope_accepted_steps > nonmonotone_accepted_steps
        or not 0.0 <= nonmonotone_envelope_max_relative_increase <= 0.000100000001
        or (
            nonmonotone_envelope_reference >= 0.0
            and (
                steady_best_residual < 0.0
                or nonmonotone_envelope_reference < steady_best_residual
                or nonmonotone_envelope_reference
                > steady_best_residual * 1.0001 * (1.0 + 64.0 * sys.float_info.epsilon)
                or nonmonotone_bridge_disabled
                or steady_reconstruction_blend != 1.0
            )
        )
        or (
            nonmonotone_envelope_accepted_steps == 0
            and nonmonotone_envelope_max_relative_increase != 0.0
        )
        or (implicit_bridge_active and implicit_bridge_disabled)
        or implicit_bridge_cfl <= 0.0
        or implicit_bridge_entry_best_residual < -1.0
        or implicit_bridge_accepted_steps > implicit_bridge_attempts
        or implicit_bridge_rejected_steps
        != implicit_bridge_attempts - implicit_bridge_accepted_steps
        or implicit_bridge_steps_since_best > 500
        or implicit_bridge_meaningful_best_improvements
        > implicit_bridge_accepted_steps
        or implicit_bridge_residual_minimum < -1.0
        or implicit_bridge_residual_maximum < -1.0
        or implicit_bridge_maximum_relative_growth < 0.0
        or implicit_bridge_linear_sweeps < 3 * implicit_bridge_attempts
        or (
            implicit_bridge_accepted_steps == 0
            and (
                implicit_bridge_residual_minimum != -1.0
                or implicit_bridge_residual_maximum != -1.0
                or implicit_bridge_maximum_relative_growth != 0.0
                or implicit_bridge_meaningful_best_improvements != 0
            )
        )
        or (
            implicit_bridge_accepted_steps > 0
            and (
                implicit_bridge_entry_best_residual < 0.0
                or implicit_bridge_residual_minimum < 0.0
                or implicit_bridge_residual_maximum
                < implicit_bridge_residual_minimum
            )
        )
        or (
            nonmonotone_bypass_attempts == 0
            and (
                nonmonotone_bypass_accepted_steps != 0
                or nonmonotone_bypass_trial_evaluations != 0
                or nonmonotone_bypass_last_actual_trial_residual != -1.0
                or nonmonotone_bypass_last_gmres_ratio != 1.0
            )
        )
        or (
            nonmonotone_bridge_active
            and (steady_reconstruction_blend != 1.0 or not nonmonotone_window)
        )
        or (
            steady_fallback_mode
            and (not fallback_window or steady_jfnk_failure_streak < 2)
        )
    ):
        raise SuiteError("restart continuation steady adaptation state is invalid")
    if transient_samples and not transient_minimum <= transient_mean <= transient_maximum:
        raise SuiteError("restart continuation transient mean is outside min/max")
    if transient_samples and transient_iteration_sum + 1.0e-12 < transient_samples * transient_minimum:
        raise SuiteError("restart continuation transient iteration sum is inconsistent")
    if history_complete and not start_time:
        raise SuiteError("restart continuation complete history lacks a start time")
    if start_time:
        parse_timestamp(start_time, "restart continuation original start time")
    if total_attempted_steps < restart_step:
        raise SuiteError("restart continuation attempted-step count precedes checkpoint step")
    if (residual_rows == 0) == has_residual_step or (force_rows == 0) == has_force_step:
        raise SuiteError("restart continuation row counts and last-step flags disagree")
    residual_step = residual_step_value if has_residual_step else None
    force_step = force_step_value if has_force_step else None
    if residual_step is not None and residual_step > restart_step:
        raise SuiteError("restart continuation residual step exceeds checkpoint step")
    if force_step is not None and force_step > restart_step:
        raise SuiteError("restart continuation force step exceeds checkpoint step")
    return {
        "history_complete": history_complete,
        "total_attempted_steps": total_attempted_steps,
        "residual_output_rows": residual_rows,
        "force_output_rows": force_rows,
        "last_residual_output_step": residual_step,
        "last_force_output_step": force_step,
        "steady_initial_residual_scale": steady_initial_residual_scale,
        "steady_full_order_initial_residual": steady_full_order_initial,
        "steady_reconstruction_blend": steady_reconstruction_blend,
        "steady_full_order_accepted_steps": steady_full_order_steps,
        "steady_target_met": steady_target_met,
        "original_initial_residual_semantics": original_initial_residual_semantics,
        "nonmonotone_accepted_steps": nonmonotone_accepted_steps,
        "nonmonotone_strict_best_improvements": nonmonotone_strict_best_improvements,
        "nonmonotone_watchdog_resets": nonmonotone_watchdog_resets,
        "nonmonotone_bypass_attempts": nonmonotone_bypass_attempts,
        "nonmonotone_bypass_accepted_steps": nonmonotone_bypass_accepted_steps,
        "nonmonotone_bypass_trial_evaluations": nonmonotone_bypass_trial_evaluations,
        "nonmonotone_bypass_last_actual_trial_residual": (
            nonmonotone_bypass_last_actual_trial_residual
        ),
        "nonmonotone_bypass_last_gmres_ratio": nonmonotone_bypass_last_gmres_ratio,
        "nonmonotone_envelope_reference": nonmonotone_envelope_reference,
        "nonmonotone_envelope_accepted_steps": nonmonotone_envelope_accepted_steps,
        "nonmonotone_envelope_max_relative_increase": (
            nonmonotone_envelope_max_relative_increase
        ),
        "implicit_bridge_active": implicit_bridge_active,
        "implicit_bridge_disabled": implicit_bridge_disabled,
        "implicit_bridge_cfl": implicit_bridge_cfl,
        "implicit_bridge_entry_best_residual": implicit_bridge_entry_best_residual,
        "implicit_bridge_attempts": implicit_bridge_attempts,
        "implicit_bridge_accepted_steps": implicit_bridge_accepted_steps,
        "implicit_bridge_rejected_steps": implicit_bridge_rejected_steps,
        "implicit_bridge_steps_since_best": implicit_bridge_steps_since_best,
        "implicit_bridge_residual_minimum": implicit_bridge_residual_minimum,
        "implicit_bridge_residual_maximum": implicit_bridge_residual_maximum,
        "implicit_bridge_maximum_relative_growth": (
            implicit_bridge_maximum_relative_growth
        ),
        "implicit_bridge_meaningful_best_improvements": (
            implicit_bridge_meaningful_best_improvements
        ),
        "implicit_bridge_watchdog_stops": implicit_bridge_watchdog_stops,
        "implicit_bridge_linear_sweeps": implicit_bridge_linear_sweeps,
    }


def validate_restart(
    path: Path,
    case_id: str,
    expected_executable_hash: str,
    expected_fingerprint: str | None = None,
    expected_cells: int | None = None,
    expected_step: int | None = None,
    expected_time: float | None = None,
) -> RestartInfo:
    if not path.is_file() or path.stat().st_size < 128:
        raise SuiteError(f"{path.name} is missing, truncated, or placeholder-sized")
    try:
        with path.open("rb") as stream:
            magic = read_exact(stream, 8, "magic")
            if magic not in SUPPORTED_RESTART_FORMATS:
                raise SuiteError("unsupported restart magic")
            version = struct.unpack("<I", read_exact(stream, 4, "version"))[0]
            if version != SUPPORTED_RESTART_FORMATS[magic]:
                raise SuiteError(f"unsupported restart version {version}")
            fingerprint_size = struct.unpack("<Q", read_exact(stream, 8, "fingerprint length"))[0]
            if not 1 <= fingerprint_size <= 1024 * 1024:
                raise SuiteError("invalid restart fingerprint length")
            fingerprint = read_exact(stream, fingerprint_size, "fingerprint").decode("utf-8")
            if not FINGERPRINT.fullmatch(fingerprint):
                raise SuiteError("invalid restart mesh fingerprint")
            if expected_fingerprint is not None and fingerprint != expected_fingerprint:
                raise SuiteError("restart fingerprint does not match output metadata")
            case_size = struct.unpack("<Q", read_exact(stream, 8, "case ID length"))[0]
            if not 1 <= case_size <= 1024 * 1024:
                raise SuiteError("invalid restart case ID length")
            restart_case = read_exact(stream, case_size, "case ID").decode("utf-8")
            if restart_case != case_id:
                raise SuiteError("restart case ID mismatch")
            executable_size = struct.unpack("<Q", read_exact(stream, 8, "executable hash length"))[0]
            if executable_size != 64:
                raise SuiteError("restart executable hash length must be 64")
            restart_executable_hash = read_exact(
                stream, executable_size, "executable hash"
            ).decode("ascii")
            if not HEX64.fullmatch(restart_executable_hash):
                raise SuiteError("restart executable hash is not 64 hexadecimal characters")
            if restart_executable_hash.lower() != expected_executable_hash.lower():
                raise SuiteError("restart executable hash does not match the runner executable")
            count, step = struct.unpack("<QQ", read_exact(stream, 16, "counts"))
            physical_time = struct.unpack("<d", read_exact(stream, 8, "time"))[0]
            finite_number(physical_time, "restart time", 0.0)
            if count < 1:
                raise SuiteError("restart has no cells")
            if expected_cells is not None and count != expected_cells:
                raise SuiteError("restart cell count does not match metadata")
            if expected_step is not None and step != expected_step:
                raise SuiteError("restart step does not match run_status")
            if expected_time is not None and not close(physical_time, expected_time):
                raise SuiteError("restart time does not match run_status")
            continuation_size = struct.unpack(
                "<Q", read_exact(stream, 8, "continuation length")
            )[0]
            if continuation_size == 0 or continuation_size > 4 * 1024 * 1024:
                raise SuiteError("restart continuation size must be in [1, 4 MiB]")
            expected_size = stream.tell() + continuation_size + count * RESTART_RECORD_BYTES
            if path.stat().st_size != expected_size:
                raise SuiteError("restart size does not match its declared cell count")
            continuation = read_exact(stream, continuation_size, "continuation")
            evidence = validate_continuation(continuation, int(step), int(version))
            ids: set[int] = set()
            for _ in range(count):
                cell_id = struct.unpack("<q", read_exact(stream, 8, "cell ID"))[0]
                states = struct.unpack("<16d", read_exact(stream, 128, "cell state"))
                if cell_id in ids or not all(math.isfinite(value) for value in states):
                    raise SuiteError("restart has duplicate IDs or nonfinite state")
                if any(states[offset] <= 0.0 for offset in (0, 4, 8, 12)):
                    raise SuiteError("restart contains nonpositive density")
                ids.add(cell_id)
    except UnicodeDecodeError as exc:
        raise SuiteError("restart contains invalid UTF-8 metadata") from exc
    except OSError as exc:
        raise SuiteError(f"cannot read restart: {exc}") from exc
    return RestartInfo(
        int(count),
        int(step),
        physical_time,
        fingerprint,
        bool(evidence["history_complete"]),
        int(evidence["total_attempted_steps"]),
        int(evidence["residual_output_rows"]),
        int(evidence["force_output_rows"]),
        evidence["last_residual_output_step"],  # type: ignore[arg-type]
        evidence["last_force_output_step"],  # type: ignore[arg-type]
        float(evidence["steady_initial_residual_scale"]),
        float(evidence["steady_full_order_initial_residual"]),
        float(evidence["steady_reconstruction_blend"]),
        int(evidence["steady_full_order_accepted_steps"]),
        bool(evidence["steady_target_met"]),
        bool(evidence["original_initial_residual_semantics"]),
        int(evidence["nonmonotone_bypass_attempts"]),
        int(evidence["nonmonotone_bypass_accepted_steps"]),
        int(evidence["nonmonotone_bypass_trial_evaluations"]),
        float(evidence["nonmonotone_bypass_last_actual_trial_residual"]),
        float(evidence["nonmonotone_bypass_last_gmres_ratio"]),
        float(evidence["nonmonotone_envelope_reference"]),
        int(evidence["nonmonotone_envelope_accepted_steps"]),
        float(evidence["nonmonotone_envelope_max_relative_increase"]),
        bool(evidence["implicit_bridge_active"]),
        bool(evidence["implicit_bridge_disabled"]),
        float(evidence["implicit_bridge_cfl"]),
        float(evidence["implicit_bridge_entry_best_residual"]),
        int(evidence["implicit_bridge_attempts"]),
        int(evidence["implicit_bridge_accepted_steps"]),
        int(evidence["implicit_bridge_rejected_steps"]),
        int(evidence["implicit_bridge_steps_since_best"]),
        float(evidence["implicit_bridge_residual_minimum"]),
        float(evidence["implicit_bridge_residual_maximum"]),
        float(evidence["implicit_bridge_maximum_relative_growth"]),
        int(evidence["implicit_bridge_meaningful_best_improvements"]),
        int(evidence["implicit_bridge_watchdog_stops"]),
        int(evidence["implicit_bridge_linear_sweeps"]),
    )


def validate_metadata(
    metadata: dict,
    status: dict,
    case_id: str,
    ranks: int,
    executable_hash: str,
    transient: bool,
) -> None:
    missing = sorted(REQUIRED_METADATA - metadata.keys())
    if missing:
        raise SuiteError("metadata.json missing field(s): " + ", ".join(missing))
    missing_status = sorted(STATUS_FIELDS - status.keys())
    if missing_status:
        raise SuiteError("run_status.json missing field(s): " + ", ".join(missing_status))
    if metadata["completed"] is not True:
        raise SuiteError("metadata.json completed is not true")
    if metadata["case_id"] != case_id or status["case_id"] != case_id:
        raise SuiteError(f"case_id does not match {case_id}")
    if integer(metadata["mpi_ranks"], "metadata mpi_ranks", 1) != ranks or integer(status["mpi_ranks"], "status mpi_ranks", 1) != ranks:
        raise SuiteError("metadata/status mpi_ranks does not match requested ranks")
    if metadata["convergence_status"] not in SUCCESS_STATUSES or status["convergence_status"] != metadata["convergence_status"]:
        raise SuiteError("metadata/status convergence_status is failed or inconsistent")
    if metadata["equation_set"] != "compressible_navier_stokes_2d":
        raise SuiteError("metadata equation_set is incorrect")
    if metadata["wall_boundary_output_semantics"] != "boundary_value":
        raise SuiteError("metadata wall_boundary_output_semantics must be boundary_value")
    for key in (
        "solver_name", "solver_version", "mesh_file", "partitioner", "halo_exchange",
        "inviscid_flux", "viscous_flux", "time_integrator", "implicit_solver",
        "reconstruction", "limiter", "positivity_preservation", "wall_boundary_output_semantics",
    ):
        if not isinstance(metadata[key], str) or not metadata[key].strip():
            raise SuiteError(f"metadata {key} must be a non-empty string")
    if "metis" not in metadata["partitioner"].lower():
        raise SuiteError("metadata partitioner must identify METIS/ParMETIS")
    if any(token in metadata["halo_exchange"].lower() for token in ("allgather", "full_state", "replicated")):
        raise SuiteError("metadata halo_exchange is not neighbor-scoped")
    for key in ("full_state_replication_during_iterations", "full_mesh_replication_during_iterations"):
        if metadata[key] is not False:
            raise SuiteError(f"metadata {key} must be false")
    if not isinstance(metadata["true_bdf2_inner_loop"], bool) or not isinstance(metadata["source_dirty"], bool):
        raise SuiteError("metadata provenance/method booleans have invalid types")
    if not isinstance(metadata["git_revision"], str) or not metadata["git_revision"].strip():
        raise SuiteError("metadata git_revision must be non-null and non-empty")
    if not isinstance(metadata["executable_sha256"], str) or not HEX64.fullmatch(metadata["executable_sha256"]):
        raise SuiteError("metadata executable_sha256 must be 64 hexadecimal characters")
    if metadata["executable_sha256"].lower() != executable_hash.lower():
        raise SuiteError("metadata executable_sha256 does not match the executable used by the runner")
    for key in ("mesh_fingerprint", "case_config_fingerprint"):
        if not isinstance(metadata[key], str) or not SHA256_FINGERPRINT.fullmatch(metadata[key]):
            raise SuiteError(f"metadata {key} must use sha256:<64 hex> format")
    for key, minimum in (
        ("num_cells_global", 1), ("num_faces_global", 1), ("num_cells_owned_local", 1),
        ("num_cells_ghost_local", 0), ("partition_edge_cut", 0), ("spatial_order_claimed", 2),
        ("min_inner_iterations", 0), ("max_inner_iterations", 0),
        ("observed_min_inner_iterations", 0), ("observed_max_inner_iterations", 0),
        ("inner_target_misses", 0),
    ):
        integer(metadata[key], f"metadata {key}", minimum)
    for key in ("typical_inner_iterations", "inner_residual_reduction_target", "inner_target_converged_fraction", "last_inner_residual_ratio"):
        finite_number(metadata[key], f"metadata {key}", 0.0)
    minimum_inner = integer(metadata["min_inner_iterations"], "metadata min_inner_iterations")
    maximum_inner = integer(metadata["max_inner_iterations"], "metadata max_inner_iterations")
    observed_min = integer(metadata["observed_min_inner_iterations"], "metadata observed_min_inner_iterations")
    observed_max = integer(metadata["observed_max_inner_iterations"], "metadata observed_max_inner_iterations")
    typical_inner = float(metadata["typical_inner_iterations"])
    if maximum_inner < minimum_inner or observed_max < observed_min:
        raise SuiteError("metadata inner-iteration ranges are inverted")
    # Transient observations count nonlinear iterations and must fit the
    # configured inner-loop bounds. Steady observations count total linear
    # sweeps across primary, retry, and rescue solves in one outer attempt, so
    # their aggregate can legitimately exceed one solve's iteration maximum.
    if (transient and observed_max > maximum_inner) or (
        observed_min and observed_min < minimum_inner
    ):
        raise SuiteError("metadata observed inner iterations are outside configured bounds")
    if observed_max and not observed_min <= typical_inner <= observed_max:
        raise SuiteError("metadata typical_inner_iterations is outside the observed range")
    fraction = float(metadata["inner_target_converged_fraction"])
    if fraction > 1.0:
        raise SuiteError("metadata inner_target_converged_fraction exceeds one")
    start = parse_timestamp(metadata["start_time_utc"], "metadata start_time_utc")
    end = parse_timestamp(metadata["end_time_utc"], "metadata end_time_utc")
    if end < start:
        raise SuiteError("metadata end_time_utc precedes start_time_utc")
    if not isinstance(status["command"], str) or not status["command"].strip() or not isinstance(status["notes"], str):
        raise SuiteError("run_status command/notes has invalid type")
    integer(status["final_step"], "run_status final_step", 1)
    finite_number(status["final_physical_time"], "run_status final_physical_time", 0.0)
    finite_number(status["wall_time_seconds"], "run_status wall_time_seconds", 0.0)
    finite_number(status["residual_reduction_orders"], "run_status residual_reduction_orders")
    finite_number(
        status["full_order_residual_reduction_orders"],
        "run_status full_order_residual_reduction_orders",
    )


def validate_steady_convergence_semantics(
    metadata: dict,
    status: dict,
    residuals: list[dict[str, float | str]],
    case: dict,
) -> None:
    run = case.get("run_control", {})
    if run.get("type") != "steady":
        return
    target = finite_number(
        run.get("residual_reduction_target"),
        "steady case residual_reduction_target",
        0.0,
    )
    pseudo_ramp = integer(
        run.get("pseudo_cfl_ramp_steps"),
        "steady case pseudo_cfl_ramp_steps",
    )
    required_hold = max(50, min(250, pseudo_ramp // 10))
    initial = finite_number(
        metadata["steady_initial_residual_scale"],
        "metadata steady_initial_residual_scale",
        0.0,
    )
    full_initial = finite_number(
        metadata["steady_full_order_initial_residual"],
        "metadata steady_full_order_initial_residual",
        0.0,
    )
    final = finite_number(
        residuals[-1]["residual_l2"], "final steady residual_l2", 0.0
    )
    original_orders = finite_number(
        metadata["residual_reduction_orders"],
        "metadata residual_reduction_orders",
    )
    full_orders = finite_number(
        metadata["full_order_residual_reduction_orders"],
        "metadata full_order_residual_reduction_orders",
    )
    if initial <= 0.0 or full_initial <= 0.0 or final <= 0.0:
        raise SuiteError("completed steady output requires positive residual baselines/final")
    expected_original_orders = math.log10(initial / final)
    expected_full_orders = math.log10(full_initial / final)
    if (
        not close(float(metadata["residual_reduction_target"]), target)
        or not close(original_orders, expected_original_orders)
        or not close(full_orders, expected_full_orders)
        or not close(float(status["steady_initial_residual_scale"]), initial)
        or not close(float(status["steady_full_order_initial_residual"]), full_initial)
        or not close(float(status["residual_reduction_orders"]), original_orders)
        or not close(
            float(status["full_order_residual_reduction_orders"]), full_orders
        )
    ):
        raise SuiteError("steady residual baselines/reduction reports are inconsistent")
    if final > initial * 10.0 ** (-target) * (1.0 + 1.0e-12):
        raise SuiteError("steady residual does not meet the original-run reduction target")

    continuation = metadata["spatial_order_continuation"]
    gate = metadata["steady_convergence_gate"]
    if not isinstance(continuation, dict) or not isinstance(gate, dict):
        raise SuiteError("steady convergence metadata objects are invalid")
    derived_phase = min(500, max(1, pseudo_ramp // 4 if pseudo_ramp > 0 else 1))
    first_steps = integer(
        continuation.get("first_order_accepted_steps"),
        "steady first-order accepted steps",
    )
    ramp_steps = integer(
        continuation.get("ramp_accepted_steps"),
        "steady ramp accepted steps",
    )
    full_steps = integer(
        continuation.get("full_order_accepted_steps"),
        "steady full-order accepted steps",
    )
    if (
        first_steps != derived_phase
        or ramp_steps != derived_phase
        or full_steps < required_hold
        or integer(
            continuation.get("minimum_full_order_steps"),
            "steady minimum full-order steps",
        )
        != required_hold
        or float(continuation.get("final_reconstruction_blend", -1.0)) != 1.0
        or continuation.get("rejected_attempts_count_toward_full_order_hold")
        is not False
    ):
        raise SuiteError("steady continuation/accepted full-order hold is incomplete")
    if (
        gate.get("residual_baseline")
        != "original_run_global_initial_residual"
        or gate.get("required_full_order_accepted_steps") != required_hold
        or gate.get("rejected_attempts_count_toward_hold") is not False
        or gate.get("physics_and_positivity_gates_required") is not True
        or gate.get("full_order_initial_residual_role") != "diagnostic_only"
        or continuation.get("original_initial_residual_role")
        != "convergence_and_OUTPUT_CONTRACT_reporting_baseline"
        or continuation.get("full_order_initial_residual_role")
        != "diagnostic_only"
    ):
        raise SuiteError("steady convergence gate/baseline semantics are undocumented")
    accepted_steps = first_steps + ramp_steps + full_steps
    attempts = integer(status["final_step"], "steady final attempt", 1)
    if accepted_steps > attempts:
        raise SuiteError("steady accepted update counters exceed attempted steps")
    physics = metadata.get("physics_gates")
    if not isinstance(physics, dict) or physics.get("passed") is not True:
        raise SuiteError("completed steady output does not pass physics/positivity gates")


def validate_re200(metadata: dict, status: dict, residuals: list[dict[str, float | str]], case: dict) -> None:
    run = case.get("run_control", {})
    dt = finite_number(run.get("time_step"), "Re200 case time_step", 0.0)
    final_time = finite_number(run.get("final_time"), "Re200 case final_time", 0.0)
    expected_steps_float = final_time / dt
    expected_steps = round(expected_steps_float)
    if not close(expected_steps_float, float(expected_steps)):
        raise SuiteError("Re200 final_time is not an integral multiple of dt")
    if integer(status["final_step"], "Re200 final_step") != expected_steps:
        raise SuiteError(f"Re200 final_step must be exactly {expected_steps}")
    if not close(float(status["final_physical_time"]), final_time):
        raise SuiteError(f"Re200 final_physical_time must be exactly {final_time}")
    if metadata["true_bdf2_inner_loop"] is not True or metadata["convergence_status"] != "statistically_periodic":
        raise SuiteError("Re200 requires a true BDF2 inner loop and statistically_periodic status")
    minimum = integer(run.get("min_inner_iterations"), "Re200 case min_inner_iterations", 1)
    maximum = integer(run.get("max_inner_iterations"), "Re200 case max_inner_iterations", minimum)
    if integer(metadata["min_inner_iterations"], "metadata min_inner_iterations") != minimum:
        raise SuiteError("Re200 metadata min_inner_iterations disagrees with case")
    if integer(metadata["max_inner_iterations"], "metadata max_inner_iterations") != maximum:
        raise SuiteError("Re200 metadata max_inner_iterations disagrees with case")
    observed_min = integer(metadata["observed_min_inner_iterations"], "observed min inner", minimum)
    observed_max = integer(metadata["observed_max_inner_iterations"], "observed max inner", observed_min)
    if observed_max > maximum:
        raise SuiteError("Re200 observed inner iterations exceed configured maximum")
    target = finite_number(metadata["inner_residual_reduction_target"], "Re200 inner target", 0.0)
    if target > float(run.get("inner_residual_reduction_target", 1.0e-3)):
        raise SuiteError("Re200 inner residual target is weaker than the case requirement")
    if float(metadata["inner_target_converged_fraction"]) < 0.95:
        raise SuiteError("Re200 inner_target_converged_fraction is below 0.95")
    for index, row in enumerate(residuals, 2):
        if not close(float(row["dt"]), dt):
            raise SuiteError(f"residuals.csv:{index} Re200 dt does not equal {dt}")
        inner = integer(row["inner_iter"], f"residuals.csv:{index} inner_iter")
        if not minimum <= inner <= maximum:
            raise SuiteError(f"residuals.csv:{index} inner_iter is outside configured bounds")


def validate_output(
    output_dir: Path,
    case_id: str,
    ranks: int,
    case_file: Path,
    executable_hash: str,
) -> OutputSummary:
    if not output_dir.is_dir():
        raise SuiteError(f"output directory is absent: {output_dir}")
    metadata = read_json_object(output_dir / "metadata.json")
    status = read_json_object(output_dir / "run_status.json")
    case = read_json_object(case_file)
    if case.get("case_id") != case_id:
        raise SuiteError("case file case_id changed during validation")
    validate_metadata(
        metadata,
        status,
        case_id,
        ranks,
        executable_hash,
        case.get("run_control", {}).get("type") == "transient",
    )
    residuals, forces = validate_histories(output_dir, case_id, status, case)
    validate_steady_convergence_semantics(metadata, status, residuals, case)
    validate_partitions(output_dir, metadata, ranks)
    validate_vtu(output_dir, metadata, status, ranks)
    restart = validate_restart(
        output_dir / "restart_final.bin",
        case_id,
        executable_hash,
        expected_fingerprint=(
            metadata["mesh_fingerprint"] + "|" + metadata["case_config_fingerprint"]
        ),
        expected_cells=integer(metadata["num_cells_global"], "metadata num_cells_global", 1),
        expected_step=integer(status["final_step"], "status final_step", 1),
        expected_time=float(status["final_physical_time"]),
    )
    if not restart.history_complete:
        raise SuiteError("final restart continuation does not prove complete history")
    if restart.residual_output_rows != len(residuals) or restart.force_output_rows != len(forces):
        raise SuiteError("final restart continuation row counts disagree with CSV histories")
    final_step = integer(status["final_step"], "status final_step", 1)
    if restart.last_residual_output_step != final_step or restart.last_force_output_step != final_step:
        raise SuiteError("final restart continuation last output steps disagree with run_status")
    if case.get("run_control", {}).get("type") == "steady":
        if (
            not restart.original_initial_residual_semantics
            or not close(
                restart.steady_initial_residual_scale,
                float(metadata["steady_initial_residual_scale"]),
            )
            or not close(
                restart.steady_full_order_initial_residual,
                float(metadata["steady_full_order_initial_residual"]),
            )
            or restart.steady_reconstruction_blend != 1.0
            or restart.steady_full_order_accepted_steps
            != integer(
                metadata["spatial_order_continuation"]["full_order_accepted_steps"],
                "metadata full-order accepted steps",
            )
            or not restart.steady_target_met
        ):
            raise SuiteError("final restart steady convergence evidence is inconsistent")
    stdout = output_dir / "stdout.log"
    if not stdout.is_file() or stdout.stat().st_size == 0:
        raise SuiteError("stdout.log is missing or empty")
    if case_id == "cylinder_m010_laminar_re200":
        validate_re200(metadata, status, residuals, case)
    return OutputSummary(metadata, status)


def manifest_key(row: dict[str, str]) -> tuple[str, ...]:
    if row.get("run_role") == "primary":
        return row.get("case_id", ""), "primary"
    return row.get("case_id", ""), row.get("run_role", ""), row.get("mpi_ranks", "")


def read_manifest(path: Path) -> dict[tuple[str, ...], dict[str, str]]:
    if not path.exists():
        return {}
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames not in (MANIFEST_FIELDS, LEGACY_MANIFEST_FIELDS):
                raise SuiteError(f"manifest header mismatch in {path}; refusing to overwrite existing data")
            rows: dict[tuple[str, ...], dict[str, str]] = {}
            for raw in reader:
                row = {field: raw.get(field, "") for field in MANIFEST_FIELDS}
                key = manifest_key(row)
                if key in rows:
                    raise SuiteError(f"duplicate manifest row key: {key}")
                rows[key] = row
            return rows
    except OSError as exc:
        raise SuiteError(f"cannot read manifest {path}: {exc}") from exc


def manifest_sort_key(row: dict[str, str]) -> tuple[object, ...]:
    try:
        ranks = int(row.get("mpi_ranks", ""))
    except ValueError:
        ranks = sys.maxsize
    role_order = 0 if row.get("run_role") == "primary" else 1
    return row.get("case_id", ""), role_order, row.get("run_role", ""), ranks, row.get("output_dir", "")


def write_manifest(path: Path, rows: Iterable[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle = tempfile.NamedTemporaryFile(
        mode="w", newline="", encoding="utf-8", prefix=f".{path.name}.", suffix=".tmp",
        dir=path.parent, delete=False,
    )
    temporary = Path(handle.name)
    try:
        with handle:
            writer = csv.DictWriter(handle, fieldnames=MANIFEST_FIELDS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(sorted(rows, key=manifest_sort_key))
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def git_revision() -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(SOLVER_ROOT), "rev-parse", "HEAD"], stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, timeout=5, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    return result.stdout.strip() if result.returncode == 0 else ""


def row_from_summary(
    spec: RunSpec,
    summary: OutputSummary,
    command: str,
    start: str,
    end: str,
    elapsed: float,
    exit_code: int,
) -> dict[str, str]:
    status = summary.status
    return {
        "case_id": spec.case_id,
        "run_role": spec.role,
        "case_file": str(spec.case_file),
        "output_dir": str(spec.output_dir),
        "command": command,
        "mpi_ranks": str(spec.ranks),
        "start_time_utc": start,
        "end_time_utc": end,
        "wall_time_seconds": f"{elapsed:.6f}",
        "final_step": str(status["final_step"]),
        "final_physical_time": str(status["final_physical_time"]),
        "convergence_status": str(status["convergence_status"]),
        "residual_reduction_orders": str(status["residual_reduction_orders"]),
        "git_revision": str(summary.metadata["git_revision"]),
        "executable_sha256": str(summary.metadata["executable_sha256"]),
        "exit_code": str(exit_code),
    }


def failure_row(
    spec: RunSpec,
    command: str,
    start: str,
    end: str,
    elapsed: float,
    revision: str,
    executable_hash: str,
    exit_code: int,
    work_dir: Path,
) -> dict[str, str]:
    status: dict = {}
    try:
        status = read_json_object(work_dir / "run_status.json")
    except SuiteError:
        pass
    return {
        "case_id": spec.case_id, "run_role": spec.role, "case_file": str(spec.case_file),
        "output_dir": str(spec.output_dir), "command": command, "mpi_ranks": str(spec.ranks),
        "start_time_utc": start, "end_time_utc": end,
        "wall_time_seconds": f"{elapsed:.6f}", "final_step": str(status.get("final_step", "")),
        "final_physical_time": str(status.get("final_physical_time", "")),
        "convergence_status": "failed",
        "residual_reduction_orders": str(status.get("residual_reduction_orders", "")),
        "git_revision": revision, "executable_sha256": executable_hash,
        "exit_code": str(exit_code),
    }


def skipped_row(spec: RunSpec, summary: OutputSummary) -> dict[str, str]:
    return row_from_summary(
        spec,
        summary,
        "unknown (execution skipped; original mpirun command/environment was not recorded)",
        str(summary.metadata["start_time_utc"]),
        str(summary.metadata["end_time_utc"]),
        float(summary.status["wall_time_seconds"]),
        0,
    )


def remove_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(path)


def safe_promote(candidate: Path, final: Path) -> None:
    """Promote a validated directory, rolling the old result back on failure."""
    if candidate == final:
        return
    backup: Path | None = None
    if final.exists() or final.is_symlink():
        backup = final.parent / f".{final.name}.backup-{os.getpid()}-{time.time_ns()}"
        os.replace(final, backup)
    try:
        os.replace(candidate, final)
    except BaseException as promotion_error:
        if backup is not None:
            try:
                os.replace(backup, final)
            except BaseException as rollback_error:
                raise SuiteError(
                    f"promotion failed ({promotion_error}); rollback also failed ({rollback_error})"
                ) from rollback_error
        raise SuiteError(f"promotion failed; previous output restored: {promotion_error}") from promotion_error
    if backup is not None:
        try:
            remove_path(backup)
        except OSError as exc:
            print(f"warning: promoted output but could not remove backup {backup}: {exc}", file=sys.stderr)


def preserve_failed_staging(staging: Path, case_id: str) -> Path:
    destination = staging.parent / (
        f".{case_id}.failed-{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}-"
        f"{os.getpid()}-{time.time_ns()}"
    )
    os.replace(staging, destination)
    return destination


def final_is_explicitly_incomplete(directory: Path) -> bool:
    """Return true only when solver evidence explicitly marks a final directory incomplete."""
    try:
        metadata = read_json_object(directory / "metadata.json")
    except SuiteError:
        return False
    if metadata.get("completed") is False or metadata.get("convergence_status") == "failed":
        return True
    try:
        status = read_json_object(directory / "run_status.json")
    except SuiteError:
        return False
    return status.get("convergence_status") == "failed"


def find_resume_candidate(spec: RunSpec, executable_hash: str) -> tuple[Path, Path] | None:
    directories: list[Path] = []
    if spec.output_dir.is_dir() and final_is_explicitly_incomplete(spec.output_dir):
        directories.append(spec.output_dir)
    directories.extend(
        path for path in spec.output_dir.parent.glob(f".{spec.case_id}.failed-*") if path.is_dir()
    )
    valid: list[tuple[int, Path, Path]] = []
    for directory in directories:
        restart = directory / RESTART_NAME
        try:
            expected_fingerprint: str | None = None
            metadata_path = directory / "metadata.json"
            if metadata_path.is_file():
                metadata = read_json_object(metadata_path)
                mesh = metadata.get("mesh_fingerprint")
                case_config = metadata.get("case_config_fingerprint")
                if (
                    not isinstance(mesh, str)
                    or not SHA256_FINGERPRINT.fullmatch(mesh)
                    or not isinstance(case_config, str)
                    or not SHA256_FINGERPRINT.fullmatch(case_config)
                ):
                    raise SuiteError("checkpoint metadata fingerprints are missing or invalid")
                expected_fingerprint = mesh + "|" + case_config
            validate_restart(
                restart,
                spec.case_id,
                executable_hash,
                expected_fingerprint=expected_fingerprint,
            )
            modified = restart.stat().st_mtime_ns
        except (SuiteError, OSError):
            continue
        valid.append((modified, directory, restart))
    if not valid:
        return None
    _, directory, restart = max(valid, key=lambda item: item[0])
    return directory, restart


def capture_process(
    argv: Sequence[str],
    env: dict[str, str],
    work_dir: Path,
    preserve_log_history: bool,
) -> tuple[int, str]:
    capture = work_dir / ".runner-stdout.log"
    stdout = work_dir / "stdout.log"
    launch_error = ""
    exit_code = 127
    try:
        with capture.open("wb") as log:
            if preserve_log_history and stdout.is_file():
                with stdout.open("rb") as previous:
                    shutil.copyfileobj(previous, log)
            try:
                process = subprocess.Popen(
                    list(argv), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=False, bufsize=0, env=env,
                )
                process_stdout = process.stdout
                if process_stdout is None:
                    raise OSError("subprocess stdout pipe was not created")
                for block in iter(lambda: process_stdout.read(65536), b""):
                    console = getattr(sys.stdout, "buffer", None)
                    if console is not None:
                        console.write(block)
                        console.flush()
                    else:
                        sys.stdout.write(block.decode("utf-8", errors="replace"))
                        sys.stdout.flush()
                    log.write(block)
                    log.flush()
                exit_code = process.wait()
            except OSError as exc:
                launch_error = f"could not launch command: {exc}"
                log.write((launch_error + "\n").encode())
        os.replace(capture, stdout)
    except OSError as exc:
        launch_error = f"could not preserve stdout.log: {exc}"
    return exit_code, launch_error


def execute_run(
    config: SuiteConfig,
    spec: RunSpec,
    revision: str,
    executable_hash: str,
) -> RunOutcome:
    if config.resume:
        try:
            summary = validate_output(spec.output_dir, spec.case_id, spec.ranks, spec.case_file, executable_hash)
        except SuiteError as exc:
            print(f"[resume] {spec.case_id} np={spec.ranks}: incomplete ({exc})")
        else:
            print(f"[resume] {spec.case_id} np={spec.ranks}: complete; skipped")
            return RunOutcome(True, skipped_row(spec, summary), True)

    env, env_changes = mpi_environment(config.mpirun, spec.ranks)
    resume_candidate = find_resume_candidate(spec, executable_hash) if config.resume else None
    if resume_candidate is not None:
        work_dir, restart = resume_candidate
        resumed_in_place = True
    else:
        work_dir = spec.output_dir.parent / f".{spec.output_dir.name}.tmp-<generated>"
        restart = None
        resumed_in_place = False

    if config.dry_run:
        command = command_for(config, spec, work_dir, restart)
        print(f"[dry-run] {quoted_command(command, env_changes)}")
        return RunOutcome(True, None)

    spec.output_dir.parent.mkdir(parents=True, exist_ok=True)
    if not resumed_in_place:
        work_dir = Path(tempfile.mkdtemp(prefix=f".{spec.output_dir.name}.tmp-", dir=spec.output_dir.parent))
    command_argv = command_for(config, spec, work_dir, restart)
    command = quoted_command(command_argv, env_changes)
    print(f"[run] {spec.case_id} role={spec.role} np={spec.ranks}")
    print(f"[command] {command}")
    start = utc_now()
    started = time.monotonic()
    exit_code, launch_error = capture_process(command_argv, env, work_dir, resumed_in_place)
    elapsed = time.monotonic() - started
    end = utc_now()

    summary: OutputSummary | None = None
    validation_error = ""
    if exit_code == 0 and not launch_error:
        try:
            summary = validate_output(work_dir, spec.case_id, spec.ranks, spec.case_file, executable_hash)
        except SuiteError as exc:
            validation_error = str(exc)
    promotion_error = ""
    if exit_code == 0 and summary is not None and not launch_error:
        try:
            safe_promote(work_dir, spec.output_dir)
        except SuiteError as exc:
            promotion_error = str(exc)
        else:
            print(f"[ok] {spec.case_id} np={spec.ranks} in {elapsed:.3f}s")
            return RunOutcome(
                True,
                row_from_summary(spec, summary, command, start, end, elapsed, exit_code),
            )

    diagnostic = launch_error or validation_error or promotion_error or f"solver exited with status {exit_code}"
    diagnostic_dir = work_dir
    if not resumed_in_place and work_dir.exists():
        diagnostic_dir = preserve_failed_staging(work_dir, spec.case_id)
    print(
        f"[failed] {spec.case_id} np={spec.ranks}: {diagnostic}; partial diagnostics: {diagnostic_dir}",
        file=sys.stderr,
    )
    return RunOutcome(
        False,
        failure_row(
            spec, command, start, end, elapsed, revision, executable_hash,
            exit_code if not promotion_error else 74, diagnostic_dir,
        ),
    )


def build_specs(config: SuiteConfig) -> list[RunSpec]:
    specs = [
        RunSpec(case_id, config.cases[case_id], config.results_dir / case_id, config.ranks, "primary")
        for case_id in sorted(config.cases)
    ]
    if config.comparisons_dir is not None:
        for case_id in sorted(config.comparison_cases):
            for ranks in sorted(set(config.comparison_ranks)):
                specs.append(
                    RunSpec(
                        case_id, config.cases[case_id],
                        config.comparisons_dir / case_id / f"np{ranks}", ranks, "rank_comparison",
                    )
                )
    return specs


def reusable_manifest_row(
    existing: dict[str, str], current: dict[str, str], spec: RunSpec, executable_hash: str,
) -> bool:
    try:
        return (
            existing.get("exit_code") == "0"
            and existing.get("convergence_status") in SUCCESS_STATUSES
            and existing.get("executable_sha256", "").lower() == executable_hash.lower()
            and existing.get("final_step") == current.get("final_step")
            and existing.get("final_physical_time") == current.get("final_physical_time")
            and existing.get("git_revision") == current.get("git_revision")
            and bool(existing.get("command"))
            and bool(existing.get("start_time_utc"))
            and bool(existing.get("end_time_utc"))
            and Path(existing.get("case_file", "")).resolve() == spec.case_file
            and Path(existing.get("output_dir", "")).resolve() == spec.output_dir
        )
    except OSError:
        return False


def run_suite(config: SuiteConfig, manifest_path: Path = MANIFEST_PATH) -> int:
    rows = read_manifest(manifest_path) if not config.dry_run else {}
    revision = git_revision()
    failures = 0
    for spec in build_specs(config):
        executable_hash = sha256_file(config.solver)
        outcome = execute_run(config, spec, revision, executable_hash)
        if not outcome.success:
            failures += 1
        if outcome.row is not None:
            key = manifest_key(outcome.row)
            existing = rows.get(key)
            row = outcome.row
            if outcome.skipped and existing is not None and reusable_manifest_row(
                existing, outcome.row, spec, executable_hash,
            ):
                row = existing
            rows[key] = row
            write_manifest(manifest_path, rows.values())
    if failures:
        print(f"suite failed: {failures} run(s) failed", file=sys.stderr)
        return 1
    print("dry run complete; no commands executed or files written" if config.dry_run else "suite complete")
    return 0


def validate_comparisons(
    comparisons_dir: Path | None,
    ranks: Sequence[int],
    cases: Sequence[str],
    discovered: dict[str, Path],
) -> tuple[tuple[int, ...], tuple[str, ...]]:
    unique_ranks = tuple(sorted(set(ranks)))
    unique_cases = tuple(dict.fromkeys(cases))
    if comparisons_dir is None:
        return unique_ranks, unique_cases
    if len(unique_ranks) < 2 or 8 not in unique_ranks:
        raise SuiteError("rank comparisons require at least two distinct ranks including np=8")
    unknown = sorted(set(unique_cases) - discovered.keys())
    if unknown:
        raise SuiteError("unknown comparison case_id(s): " + ", ".join(unknown))
    naca = [case for case in unique_cases if case.startswith("naca0012_")]
    cylinder = [case for case in unique_cases if case.startswith("cylinder_")]
    if len(unique_cases) != 2 or len(naca) != 1 or len(cylinder) != 1:
        raise SuiteError("comparison cases must designate exactly one NACA and one cylinder case")
    return unique_ranks, unique_cases


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", required=True, help="solver executable")
    parser.add_argument("--cases-dir", required=True, type=Path)
    parser.add_argument("--results-dir", required=True, type=Path)
    parser.add_argument("--ranks", type=positive_int, default=8)
    parser.add_argument("--comparisons-dir", type=Path)
    parser.add_argument("--comparison-ranks", nargs="+", type=positive_int, default=list(DEFAULT_COMPARISON_RANKS))
    parser.add_argument("--comparison-cases", nargs="+", default=list(DEFAULT_COMPARISON_CASES))
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--mpirun", default="mpirun", help="mpirun/mpiexec executable")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(argv if argv is not None else sys.argv[1:])
    if arguments == ["--self-test"]:
        return run_self_tests()
    args = make_parser().parse_args(arguments)
    try:
        cases = discover_cases(args.cases_dir.resolve())
        comparison_ranks, comparison_cases = validate_comparisons(
            args.comparisons_dir, args.comparison_ranks, args.comparison_cases, cases,
        )
        config = SuiteConfig(
            solver=resolve_executable(args.solver, "solver"),
            mpirun=resolve_executable(args.mpirun, "mpirun"),
            cases=cases,
            results_dir=args.results_dir.resolve(),
            ranks=args.ranks,
            comparisons_dir=args.comparisons_dir.resolve() if args.comparisons_dir else None,
            comparison_ranks=comparison_ranks,
            comparison_cases=comparison_cases,
            resume=args.resume,
            dry_run=args.dry_run,
        )
        return run_suite(config)
    except SuiteError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


# Tests create fixtures only below SOLVER_ROOT; TemporaryDirectory removes all of them.
class RunSuiteTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix=".run-suite-test-", dir=SOLVER_ROOT)
        self.root = Path(self.temporary.name)
        self.cases_dir = self.root / "cases"
        self.cases_dir.mkdir()
        for index, case_id in enumerate(sorted(EXPECTED_CASE_IDS)):
            transient = case_id.endswith("re200")
            case = {
                "case_id": case_id,
                "physics": {"mode": "inviscid" if "inviscid" in case_id else "laminar"},
                "run_control": {
                    "type": "transient" if transient else "steady",
                    "time_step": 0.01 if transient else None,
                    "final_time": 300.0 if transient else None,
                     "min_inner_iterations": 5 if transient else 3,
                     "max_inner_iterations": 1000 if transient else 50,
                     "inner_residual_reduction_target": 0.001,
                     "residual_reduction_target": None if transient else 4.0,
                     "pseudo_cfl_ramp_steps": 0 if transient else 10,
                },
            }
            (self.cases_dir / f"case-{index}.json").write_text(json.dumps(case), encoding="utf-8")
        self.mpirun = self.root / "fake mpirun.py"
        self.solver = self.root / "fake solver.py"
        self._write_executable(
            self.mpirun,
            """#!/usr/bin/env python3
import os, subprocess, sys
if sys.argv[1:] == ['--version']:
    print('mpirun (Open MPI) fake'); raise SystemExit(0)
args = sys.argv[1:]
if len(args) < 3 or args[0] != '-np': raise SystemExit(91)
env = os.environ.copy(); env['FAKE_MPI_RANKS'] = args[1]
raise SystemExit(subprocess.call(args[2:], env=env))
""",
        )
        self._write_executable(self.solver, self._fake_solver_source())
        self.cases = discover_cases(self.cases_dir)
        self.executable_hash = sha256_file(self.solver)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def _write_executable(path: Path, source: str) -> None:
        path.write_text(source, encoding="utf-8")
        path.chmod(0o755)

    @staticmethod
    def _fake_solver_source() -> str:
        return r'''#!/usr/bin/env python3
import argparse, csv, hashlib, json, os, struct, sys
from pathlib import Path
p = argparse.ArgumentParser(); sub = p.add_subparsers(dest='command', required=True)
s = sub.add_parser('solve'); s.add_argument('--case', required=True); s.add_argument('--output', required=True)
s.add_argument('--report-level'); s.add_argument('--restart'); s.add_argument('--resume-output', action='store_true')
a = p.parse_args(); case = json.loads(Path(a.case).read_text()); cid = case['case_id']
mode = case.get('_test_mode', 'success'); out = Path(a.output); out.mkdir(parents=True, exist_ok=True)
ranks = int(os.environ['FAKE_MPI_RANKS']); final_step = 54; final_time = 0.0
exe_hash = hashlib.sha256(Path(sys.argv[0]).read_bytes()).hexdigest()
mesh_fingerprint='sha256:'+'0'*64; case_config_fingerprint='sha256:'+'1'*64
metadata = {'case_id': cid, 'solver_name': 'fake', 'solver_version': '1', 'git_revision': 'a'*40,
 'source_dirty': False, 'executable_sha256': exe_hash, 'mpi_ranks': ranks, 'mesh_file': 'fake.cgns',
 'num_cells_global': ranks, 'num_faces_global': ranks*3, 'num_cells_owned_local': 1,
 'num_cells_ghost_local': 0, 'partitioner': 'metis_kway', 'partition_edge_cut': 0,
 'halo_exchange': 'neighbor_isend_irecv', 'full_state_replication_during_iterations': False,
 'full_mesh_replication_during_iterations': False, 'equation_set': 'compressible_navier_stokes_2d',
 'inviscid_flux': 'rusanov', 'entropy_fix': None, 'viscous_flux': 'gradient', 'time_integrator': 'implicit',
 'implicit_solver': 'lu_sgs', 'reconstruction': 'least_squares', 'limiter': 'Barth-Jespersen',
 'spatial_order_claimed': 2, 'positivity_preservation': 'scaling',
 'wall_boundary_output_semantics': 'boundary_value', 'true_bdf2_inner_loop': False,
 'typical_inner_iterations': 5.0, 'min_inner_iterations': 3, 'max_inner_iterations': 50,
 'observed_min_inner_iterations': 5, 'observed_max_inner_iterations': 5,
 'inner_residual_reduction_target': 0.001, 'inner_target_misses': 0,
 'inner_target_converged_fraction': 1.0, 'last_inner_residual_ratio': 0.001,
 'start_time_utc': '2026-01-01T00:00:00Z', 'end_time_utc': '2026-01-01T00:00:01Z',
 'mesh_fingerprint': mesh_fingerprint, 'case_config_fingerprint': case_config_fingerprint,
 'steady_initial_residual_scale': 1000.0, 'steady_full_order_initial_residual': 10.0,
 'residual_reduction_target': 4.0, 'residual_reduction_orders': 4.0,
 'full_order_residual_reduction_orders': 2.0,
 'steady_convergence_gate': {'residual_baseline': 'original_run_global_initial_residual',
  'required_full_order_accepted_steps': 50, 'rejected_attempts_count_toward_hold': False,
  'physics_and_positivity_gates_required': True, 'full_order_initial_residual_role': 'diagnostic_only'},
 'spatial_order_continuation': {'first_order_accepted_steps': 2, 'ramp_accepted_steps': 2,
  'full_order_accepted_steps': 50, 'minimum_full_order_steps': 50,
  'final_reconstruction_blend': 1.0, 'rejected_attempts_count_toward_full_order_hold': False,
  'original_initial_residual_role': 'convergence_and_OUTPUT_CONTRACT_reporting_baseline',
  'full_order_initial_residual_role': 'diagnostic_only'},
 'physics_gates': {'passed': True},
 'completed': mode != 'incomplete', 'convergence_status': 'converged'}
status = {'case_id': cid, 'command': ' '.join(sys.argv), 'mpi_ranks': ranks, 'wall_time_seconds': 1.0,
 'final_step': final_step, 'final_physical_time': final_time, 'convergence_status': 'converged',
 'steady_initial_residual_scale': 1000.0, 'steady_full_order_initial_residual': 10.0,
 'residual_reduction_orders': 4.0, 'full_order_residual_reduction_orders': 2.0, 'notes': ''}
(out/'metadata.json').write_text(json.dumps(metadata)); (out/'run_status.json').write_text(json.dumps(status))
append = a.resume_output and (out/'residuals.csv').exists()
if append:
 for name in ('residuals.csv','forces.csv'):
  path=out/name
  with path.open(newline='') as f: rows=list(csv.reader(f))
  with path.open('w',newline='') as f:
   w=csv.writer(f); w.writerow(rows[0]); w.writerows(row for row in rows[1:] if int(float(row[0])) <= final_step)
def write_rows(name, header, rows):
 with (out/name).open('a' if append else 'w', newline='') as f:
  w=csv.writer(f)
  if not append: w.writerow(header)
  w.writerows(rows[-1:] if append else rows)
rh=['step','physical_time','inner_iter','cfl','dt','rho','rhou','rhov','rhoE','residual_l2','residual_linf']
fh=['step','physical_time','cl','cd','cmz','pressure_drag','viscous_drag','pressure_lift','viscous_lift']
sh=['x','y','nx','ny','pressure','cp','cf','rho','u','v','mach','tag']
viscous = 0.0 if 'inviscid' in cid else 0.01
write_rows('residuals.csv',rh,[[1,0,5,1,0,500,500,500,500,1000,1000],[final_step,0,5,1,0,.05,.05,.05,.05,.1,.1]])
write_rows('forces.csv',fh,[[1,0,0,.1,0,.1,viscous,0,viscous],[final_step,0,0,.2,0,.2,viscous,0,viscous]])
with (out/'surface.csv').open('w',newline='') as f:
 w=csv.writer(f); w.writerow(sh)
 for i in range(3): w.writerow([i,i%2,1,0,1+i*.1,i*.1,0 if 'inviscid' in cid else .01,1,0,0,.1,'wall'])
with (out/'partition_diagnostics.csv').open('w',newline='') as f:
 w=csv.writer(f); w.writerow(['rank','num_cells_owned','num_cells_ghost','num_boundary_faces','num_neighbor_ranks','neighbor_ranks','send_cells','recv_cells'])
 for rank in range(ranks): w.writerow([rank,1,0,1,0,'','',''])
points=[]; connectivity=[]; offsets=[]
for i in range(ranks):
 base=3*i; points += [i,0,0,i+0.5,0,0,i,0.5,0]; connectivity += [base,base+1,base+2]; offsets.append(3*(i+1))
arrays={'rho':[1]*ranks,'u':[1]*ranks,'v':[0]*ranks,'pressure':[1+i*.1 for i in range(ranks)],
 'mach':[.1]*ranks,'rhoE':[3]*ranks,'temperature':[1]*ranks,'owner_rank':list(range(ranks))}
cell_data=''.join(f'<DataArray Name="{k}">{" ".join(map(str,v))}</DataArray>' for k,v in arrays.items())
xml=f"""<?xml version="1.0"?><VTKFile type="UnstructuredGrid"><UnstructuredGrid><FieldData><DataArray Name="step">{final_step}</DataArray><DataArray Name="time">0</DataArray></FieldData><Piece NumberOfPoints="{ranks*3}" NumberOfCells="{ranks}"><Points><DataArray>{' '.join(map(str,points))}</DataArray></Points><Cells><DataArray Name="connectivity">{' '.join(map(str,connectivity))}</DataArray><DataArray Name="offsets">{' '.join(map(str,offsets))}</DataArray><DataArray Name="types">{' '.join(['5']*ranks)}</DataArray></Cells><CellData>{cell_data}</CellData></Piece></UnstructuredGrid></VTKFile>"""
(out/'field_final.vtu').write_text(xml)
fingerprint=(mesh_fingerprint+'|'+case_config_fingerprint).encode(); case_bytes=cid.encode(); executable_bytes=exe_hash.encode()
with (out/'residuals.csv').open(newline='') as f: residual_rows=list(csv.reader(f))[1:]
with (out/'forces.csv').open(newline='') as f: force_rows=list(csv.reader(f))[1:]
start=b'2026-01-01T00:00:00Z'; continuation=bytearray()
continuation += struct.pack('<dd',1000.0,0.1)
continuation += struct.pack('<QiidQdd',2,5,5,5.0,0,1.0,0.001)
continuation += struct.pack('<Q',len(start)) + start
continuation += struct.pack('<BBQQiQQ',1,1,0,final_step,5,len(residual_rows),len(force_rows))
continuation += struct.pack('<BQBQ',1,final_step,1,final_step)
continuation += struct.pack('<dQdddiBidB',1.0,final_step,0.1,0.1,0.1,0,0,0,0.0,0)
continuation += struct.pack('<QQQQQd',final_step,0,0,0,0,0.0)
continuation += struct.pack('<BdQiQd',0,0.1,0,0,final_step,1000.0)
continuation += struct.pack('<dQQQdd',1.0,2,2,50,10.0,0.1)
continuation += struct.pack('<BQQQiiddddQQBQQ',0,0,0,0,0,0,1.0,0.0,0.0,-1.0,0,0,1,0,0)
continuation += struct.pack('<QQdQ',0,0,1.0,0)
continuation += struct.pack('<idiQddQdB',0,0.0,0,0,0.0,1.0,0,0.0,1)
continuation += struct.pack('<Qdddd',2,0.0,0.1,0.0,0.2)
continuation += struct.pack('<QQQQQii',0,0,0,0,0,0,0)
continuation += struct.pack('<ddddQ',0.0,0.0,-1.0,-1.0,0)
continuation += struct.pack('<ddd i',1000.0,1.0,0.0,0)
continuation += struct.pack('<B',1)
continuation += struct.pack('<QQBBQQdQQ',0,0,0,0,0,0,0.0,0,0)
with (out/'restart_checkpoint.bin').open('wb') as f:
 f.write(b'CFDRST12'); f.write(struct.pack('<I',12)); f.write(struct.pack('<Q',len(fingerprint))); f.write(fingerprint)
 f.write(struct.pack('<Q',len(case_bytes))); f.write(case_bytes); f.write(struct.pack('<Q',len(executable_bytes))); f.write(executable_bytes)
 f.write(struct.pack('<QQdQ',ranks,final_step,final_time,len(continuation))); f.write(continuation)
 for i in range(ranks): f.write(struct.pack('<q16d',i,1,0,0,3,1,0,0,3,1,0,0,3,1,0,0,3))
with (out/'restart_checkpoint.bin').open('rb') as source, (out/'restart_final.bin').open('wb') as target: target.write(source.read())
(out/'history.marker').write_text((out/'history.marker').read_text()+'x' if (out/'history.marker').exists() else 'x')
print('fake solver stdout', flush=True)
if mode == 'exit': raise SystemExit(9)
'''

    def config(self, **changes: object) -> SuiteConfig:
        values: dict[str, object] = {
            "solver": self.solver, "mpirun": self.mpirun, "cases": self.cases,
            "results_dir": self.root / "results", "ranks": 8, "comparisons_dir": None,
            "comparison_ranks": DEFAULT_COMPARISON_RANKS,
            "comparison_cases": DEFAULT_COMPARISON_CASES, "resume": False, "dry_run": False,
        }
        values.update(changes)
        return SuiteConfig(**values)  # type: ignore[arg-type]

    def rewrite_case(self, case_id: str, **values: object) -> None:
        path = self.cases[case_id]
        data = json.loads(path.read_text(encoding="utf-8")); data.update(values)
        path.write_text(json.dumps(data), encoding="utf-8")

    def quiet_run(self, config: SuiteConfig, manifest: Path) -> int:
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return run_suite(config, manifest)

    def create_valid(self, case_id: str = "naca0012_m015_inviscid") -> tuple[SuiteConfig, Path, Path]:
        config = self.config(cases={case_id: self.cases[case_id]})
        manifest = self.root / f"manifest-{time.time_ns()}.csv"
        self.assertEqual(self.quiet_run(config, manifest), 0)
        return config, config.results_dir / case_id, manifest

    def assert_invalid(self, output: Path, case_id: str, pattern: str) -> None:
        with self.assertRaisesRegex(SuiteError, pattern):
            validate_output(output, case_id, 8, self.cases[case_id], self.executable_hash)

    @staticmethod
    def continuation_offset(path: Path) -> tuple[int, int]:
        data = path.read_bytes()
        position = 12
        for _ in range(3):
            size = struct.unpack_from("<Q", data, position)[0]
            position += 8 + size
        position += struct.calcsize("<QQd")
        size = struct.unpack_from("<Q", data, position)[0]
        return position + 8, size

    def test_discovery_requires_exact_case_ids(self) -> None:
        self.cases[next(iter(sorted(self.cases)))].unlink()
        with self.assertRaisesRegex(SuiteError, "missing case_id"):
            discover_cases(self.cases_dir)

    def test_invalid_metadata_is_rejected(self) -> None:
        _, output, _ = self.create_valid()
        metadata = read_json_object(output / "metadata.json"); del metadata["source_dirty"]
        (output / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        self.assert_invalid(output, "naca0012_m015_inviscid", "source_dirty")

    def test_restart_continuation_corruption_is_rejected(self) -> None:
        _, output, _ = self.create_valid()
        restart = output / "restart_final.bin"
        data = bytearray(restart.read_bytes())
        offset, size = self.continuation_offset(restart)
        self.assertGreater(size, 8)
        data[offset : offset + 8] = struct.pack("<d", math.nan)
        restart.write_bytes(data)
        self.assert_invalid(output, "naca0012_m015_inviscid", "continuation.*finite")

    def test_restart_fingerprint_mismatch_is_rejected(self) -> None:
        _, output, _ = self.create_valid()
        metadata = read_json_object(output / "metadata.json")
        metadata["mesh_fingerprint"] = "sha256:" + "2" * 64
        (output / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        self.assert_invalid(output, "naca0012_m015_inviscid", "fingerprint.*metadata")

    def test_nonnumeric_and_truncated_csv_are_rejected(self) -> None:
        _, output, _ = self.create_valid()
        baseline = self.root / "baseline"; shutil.copytree(output, baseline)
        text = (output / "residuals.csv").read_text().replace("0.1,0.1", "oops,0.1", 1)
        (output / "residuals.csv").write_text(text)
        self.assert_invalid(output, "naca0012_m015_inviscid", "numeric")
        shutil.rmtree(output); shutil.copytree(baseline, output)
        lines = (output / "forces.csv").read_text().splitlines()
        lines[-1] = ",".join(lines[-1].split(",")[:-1])
        (output / "forces.csv").write_text("\n".join(lines) + "\n")
        self.assert_invalid(output, "naca0012_m015_inviscid", "columns")

    def test_placeholder_vtu_and_restart_are_rejected(self) -> None:
        _, output, _ = self.create_valid()
        baseline = self.root / "baseline"; shutil.copytree(output, baseline)
        (output / "field_final.vtu").write_text("<VTKFile/>")
        self.assert_invalid(output, "naca0012_m015_inviscid", "placeholder")
        shutil.rmtree(output); shutil.copytree(baseline, output)
        (output / "restart_final.bin").write_bytes(b"placeholder")
        self.assert_invalid(output, "naca0012_m015_inviscid", "restart")

    def test_failed_rerun_preserves_previous_output(self) -> None:
        config, output, manifest = self.create_valid()
        before = (output / "metadata.json").read_bytes()
        self.rewrite_case("naca0012_m015_inviscid", _test_mode="exit")
        self.assertEqual(self.quiet_run(config, manifest), 1)
        self.assertEqual((output / "metadata.json").read_bytes(), before)
        self.assertTrue(any(output.parent.glob(".naca0012_m015_inviscid.failed-*")))

    def test_executed_rerun_replaces_stale_manifest(self) -> None:
        config, _, manifest = self.create_valid()
        rows = read_manifest(manifest); row = next(iter(rows.values())); row["command"] = "STALE"
        write_manifest(manifest, rows.values())
        self.assertEqual(self.quiet_run(config, manifest), 0)
        fresh = next(iter(read_manifest(manifest).values()))
        self.assertNotEqual(fresh["command"], "STALE")
        self.assertEqual(fresh["executable_sha256"], self.executable_hash)

    def test_skipped_resume_preserves_recorded_manifest(self) -> None:
        config, _, manifest = self.create_valid()
        rows = read_manifest(manifest); row = next(iter(rows.values())); row["command"] = "RECORDED"
        write_manifest(manifest, rows.values())
        resumed = self.config(cases=config.cases, resume=True)
        self.assertEqual(self.quiet_run(resumed, manifest), 0)
        self.assertEqual(next(iter(read_manifest(manifest).values()))["command"], "RECORDED")

    def test_completed_final_with_bad_provenance_is_not_resumed_in_place(self) -> None:
        case_id = "naca0012_m015_inviscid"
        config, output, manifest = self.create_valid(case_id)
        metadata = read_json_object(output / "metadata.json")
        metadata["executable_sha256"] = "b" * 64
        (output / "metadata.json").write_text(json.dumps(metadata), encoding="utf-8")
        (output / "must-not-survive").write_text("old final", encoding="utf-8")
        resumed = self.config(cases=config.cases, resume=True)
        self.assertEqual(self.quiet_run(resumed, manifest), 0)
        self.assertFalse((output / "must-not-survive").exists())
        command = next(iter(read_manifest(manifest).values()))["command"]
        self.assertNotIn("--resume-output", command)
        self.assertNotIn("--restart", command)

    def test_promotion_failure_rolls_back(self) -> None:
        final = self.root / "final"; candidate = self.root / "candidate"
        final.mkdir(); candidate.mkdir(); (final / "old").write_text("old"); (candidate / "new").write_text("new")
        real_replace = os.replace
        def replace(source, destination):
            if Path(source) == candidate and Path(destination) == final:
                raise OSError("injected promotion failure")
            real_replace(source, destination)
        with mock.patch.object(os, "replace", side_effect=replace):
            with self.assertRaisesRegex(SuiteError, "previous output restored"):
                safe_promote(candidate, final)
        self.assertEqual((final / "old").read_text(), "old")
        self.assertTrue((candidate / "new").exists())

    def test_checkpoint_resume_preserves_history_and_records_command(self) -> None:
        case_id = "naca0012_m015_inviscid"
        self.rewrite_case(case_id, _test_mode="incomplete")
        config = self.config(cases={case_id: self.cases[case_id]})
        manifest = self.root / "resume.csv"
        self.assertEqual(self.quiet_run(config, manifest), 1)
        failed = next(config.results_dir.glob(f".{case_id}.failed-*"))
        with (failed / "residuals.csv").open("a", encoding="utf-8") as stream:
            stream.write("999,0,5,1,0,0.01,0.01,0.01,0.01,0.01,0.01\n")
        with (failed / "forces.csv").open("a", encoding="utf-8") as stream:
            stream.write("999,0,0,0.2,0,0.2,0,0,0\n")
        self.rewrite_case(case_id, _test_mode="success")
        resumed = self.config(cases=config.cases, resume=True)
        self.assertEqual(self.quiet_run(resumed, manifest), 0)
        output = resumed.results_dir / case_id
        self.assertNotIn("999,", (output / "residuals.csv").read_text())
        self.assertNotIn("999,", (output / "forces.csv").read_text())
        self.assertEqual((output / "history.marker").read_text(), "xx")
        command = next(iter(read_manifest(manifest).values()))["command"]
        self.assertIn("--restart", command)
        self.assertIn("--resume-output", command)

    def test_real_cpp_diagnostic_checkpoint_parser(self) -> None:
        binary = SOLVER_ROOT / "build" / "cfd_solver"
        if not binary.is_file():
            self.skipTest("build/cfd_solver is absent; real C++ checkpoint smoke unavailable")
        case_file = (
            SOLVER_ROOT.parent
            / "cfd_solver_agentic_benchmark"
            / "inputs"
            / "cases"
            / "naca0012_m015_inviscid.json"
        )
        output = self.root / "cpp-diagnostic-output"
        command = [
            str(binary), "solve", "--case", str(case_file), "--output", str(output),
            "--report-level", "brief", "--max-steps", "1", "--progress-every", "1",
            "--flush-every", "1",
        ]
        result = subprocess.run(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, timeout=180, check=False,
        )
        checkpoint = output / RESTART_NAME
        self.assertTrue(
            checkpoint.is_file(),
            msg=f"diagnostic C++ run did not write {RESTART_NAME}:\n{result.stdout}",
        )
        info = validate_restart(
            checkpoint,
            "naca0012_m015_inviscid",
            sha256_file(binary),
        )
        self.assertGreater(info.cell_count, 0)
        self.assertGreaterEqual(info.step, 0)
        self.assertEqual(info.nonmonotone_bypass_attempts, 0)
        self.assertEqual(info.nonmonotone_bypass_accepted_steps, 0)
        self.assertEqual(info.nonmonotone_bypass_trial_evaluations, 0)
        self.assertEqual(info.nonmonotone_bypass_last_actual_trial_residual, -1.0)
        self.assertEqual(info.nonmonotone_bypass_last_gmres_ratio, 1.0)
        self.assertEqual(info.nonmonotone_envelope_reference, -1.0)
        self.assertEqual(info.nonmonotone_envelope_accepted_steps, 0)
        self.assertEqual(info.nonmonotone_envelope_max_relative_increase, 0.0)
        self.assertFalse(info.implicit_bridge_active)
        self.assertFalse(info.implicit_bridge_disabled)
        self.assertEqual(info.implicit_bridge_cfl, 0.1)
        self.assertEqual(info.implicit_bridge_attempts, 0)
        self.assertEqual(info.implicit_bridge_accepted_steps, 0)
        self.assertFalse((self.root / "results" / "naca0012_m015_inviscid").exists())


def run_self_tests() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RunSuiteTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())

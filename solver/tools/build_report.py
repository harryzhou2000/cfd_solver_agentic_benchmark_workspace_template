#!/usr/bin/env python3
"""Build report artifacts from complete, successful CFD result directories.

This program validates report-input consistency and renders evidence already
written by the solver.  It never creates histories or upgrades a failed status.
It cannot, by itself, prove physical correctness, source-code behaviour, or
that a result package originated from a genuine solver execution.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np

from plot_results import (
    PlotError,
    _normalise,
    final_field_path,
    generate_case_figures,
    lift_spectrum,
    read_case_tables,
    read_field,
)


REQUIRED_CASES = (
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
)
ALLOWED_STATUS = {"converged", "statistically_periodic"}
MANIFEST_COLUMNS = ("figure_file", "case_id", "figure_type", "variable", "source_file", "caption")
PARTITION_COLUMNS = (
    "rank", "num_cells_owned", "num_cells_ghost", "num_boundary_faces",
    "num_neighbor_ranks", "neighbor_ranks", "send_cells", "recv_cells",
)
RUN_MANIFEST_COLUMNS = (
    "record_type", "case_id", "source_directory", "command", "mpi_ranks", "wall_time_seconds", "final_step",
    "final_physical_time", "convergence_status", "residual_reduction_orders", "notes",
)

STEADY_MIN_HISTORY_ROWS = 50
STEADY_TERMINAL_WINDOW = 20
RE200_MIN_SPECTRUM_SAMPLES = 512
INVISCID_H0_P99_LIMIT = 0.05
INVISCID_H0_MAX_LIMIT = 0.10
RANK_CD_ABSOLUTE_TOLERANCE = 1.0e-4
RANK_CD_RELATIVE_TOLERANCE = 0.01
RANK_CL_ABSOLUTE_TOLERANCE = 1.0e-4
RANK_RESIDUAL_ORDER_TOLERANCE = 0.25
METHOD_METADATA_FIELDS = (
    "equation_set", "inviscid_flux", "viscous_flux", "time_integrator",
    "implicit_solver", "reconstruction", "limiter", "positivity_preservation",
    "wall_boundary_output_semantics", "partitioner", "halo_exchange",
)


class BuildError(RuntimeError):
    """A result set cannot support an honest final report."""


@dataclass
class CaseData:
    case_id: str
    directory: Path
    metadata: dict
    status: dict
    residuals: list[dict[str, float | str]]
    forces: list[dict[str, float | str]]
    surface: list[dict[str, float | str]]
    field_path: Path
    case_input: dict[str, Any]
    partition_rows: list[dict[str, int | str]]


def _read_json(path: Path) -> dict:
    if not path.is_file():
        raise BuildError(f"missing {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise BuildError(f"invalid JSON: {path}") from exc
    if not isinstance(value, dict):
        raise BuildError(f"{path} must contain an object")
    return value


def _number(value: object, label: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise BuildError(f"{label} must be numeric") from exc
    if not math.isfinite(result):
        raise BuildError(f"{label} must be finite")
    return result


def default_case_inputs_dir() -> Path:
    """Return the checked-in benchmark case controls, without copying them."""
    return Path(__file__).resolve().parents[2] / "cfd_solver_agentic_benchmark" / "inputs" / "cases"


def load_case_inputs(case_inputs_dir: Path) -> dict[str, dict[str, Any]]:
    if not case_inputs_dir.is_dir():
        raise BuildError(f"missing benchmark case-input directory: {case_inputs_dir}")
    inputs: dict[str, dict[str, Any]] = {}
    for case_id in REQUIRED_CASES:
        payload = _read_json(case_inputs_dir / f"{case_id}.json")
        if payload.get("case_id") != case_id:
            raise BuildError(f"case input for {case_id} has a mismatched case_id")
        inputs[case_id] = payload
    return inputs


def _read_partition_diagnostics(case_dir: Path) -> list[dict[str, int | str]]:
    """Read the CSV contract used by the submitted C++ writer.

    JSON diagnostics are permitted by the benchmark contract, but this report needs
    per-rank rows for its table.  A JSON-only package is therefore rejected with an
    actionable message rather than silently dropping MPI evidence.
    """
    path = case_dir / "partition_diagnostics.csv"
    if not path.is_file():
        if (case_dir / "partition_diagnostics.json").is_file():
            raise BuildError(f"{case_dir}: report automation requires per-rank partition_diagnostics.csv")
        raise BuildError(f"missing {path}")
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != list(PARTITION_COLUMNS):
            raise BuildError(f"{path}: exact header required: {','.join(PARTITION_COLUMNS)}")
        raw_rows = list(reader)
    if not raw_rows:
        raise BuildError(f"{path}: no per-rank diagnostics")
    rows: list[dict[str, int | str]] = []
    ranks: set[int] = set()
    for line, row in enumerate(raw_rows, 2):
        converted: dict[str, int | str] = {}
        for name in PARTITION_COLUMNS[:5]:
            value = _number(row.get(name), f"{path}:{line} {name}")
            if int(value) != value or value < 0:
                raise BuildError(f"{path}:{line} {name} must be a non-negative integer")
            converted[name] = int(value)
        rank = int(converted["rank"])
        if rank in ranks:
            raise BuildError(f"{path}: duplicate rank {rank}")
        ranks.add(rank)
        for name in PARTITION_COLUMNS[5:]:
            converted[name] = row.get(name, "")
        rows.append(converted)
    return sorted(rows, key=lambda entry: int(entry["rank"]))


def _split_integer_list(value: object, label: str) -> list[int]:
    text = str(value).strip()
    if not text:
        return []
    result: list[int] = []
    for token in text.split(";"):
        number = _number(token, label)
        if number < 0 or int(number) != number:
            raise BuildError(f"{label} must contain semicolon-separated non-negative integers")
        result.append(int(number))
    return result


def _validate_partition_evidence(case_id: str, metadata: dict[str, Any],
                                 rows: list[dict[str, int | str]], ranks: int) -> None:
    if len(rows) != ranks or [int(row["rank"]) for row in rows] != list(range(ranks)):
        raise BuildError(f"{case_id}: partition diagnostics must have one row for every MPI rank")
    owned_total = sum(int(row["num_cells_owned"]) for row in rows)
    expected_cells = int(_number(metadata.get("num_cells_global"), f"{case_id} num_cells_global"))
    if owned_total != expected_cells:
        raise BuildError(
            f"{case_id}: partition owned-cell total {owned_total} does not equal global cell count {expected_cells}")
    for row in rows:
        rank = int(row["rank"])
        expected_neighbors = int(row["num_neighbor_ranks"])
        neighbors = _split_integer_list(row["neighbor_ranks"], f"{case_id} rank {rank} neighbor_ranks")
        sends = _split_integer_list(row["send_cells"], f"{case_id} rank {rank} send_cells")
        receives = _split_integer_list(row["recv_cells"], f"{case_id} rank {rank} recv_cells")
        if not (len(neighbors) == len(sends) == len(receives) == expected_neighbors):
            raise BuildError(
                f"{case_id}: rank {rank} neighbor/send/receive list lengths must equal num_neighbor_ranks")
        if any(neighbor == rank or neighbor >= ranks for neighbor in neighbors):
            raise BuildError(f"{case_id}: rank {rank} has an invalid neighbor rank")


def _validate_force_evidence(case_id: str, case_input: dict[str, Any],
                             forces: list[dict[str, float | str]]) -> None:
    inviscid = case_input["physics"]["mode"] == "inviscid"
    for row_number, row in enumerate(forces, start=2):
        cd = float(row["cd"])
        cl = float(row["cl"])
        pressure_drag = float(row["pressure_drag"])
        viscous_drag = float(row["viscous_drag"])
        pressure_lift = float(row["pressure_lift"])
        viscous_lift = float(row["viscous_lift"])
        # CSV values are written with six significant digits.  Allow the
        # independent decimal roundoff of the total and both split columns.
        if not math.isclose(cd, pressure_drag + viscous_drag, rel_tol=2.0e-5, abs_tol=2.0e-6):
            raise BuildError(f"{case_id}: forces.csv row {row_number} has an inconsistent drag split")
        if not math.isclose(cl, pressure_lift + viscous_lift, rel_tol=2.0e-5, abs_tol=2.0e-6):
            raise BuildError(f"{case_id}: forces.csv row {row_number} has an inconsistent lift split")
        if inviscid and max(abs(viscous_drag), abs(viscous_lift)) > 1.0e-8:
            raise BuildError(f"{case_id}: inviscid forces.csv row {row_number} has a non-negligible viscous force")


def _validate_surface_evidence(case_id: str, case_input: dict[str, Any],
                               surface: list[dict[str, float | str]]) -> None:
    expected_wall_tags = {
        str(tag) for tag, kind in case_input["boundary_conditions"].items()
        if "wall" in str(kind).lower()
    }
    observed_tags = {str(row["tag"]) for row in surface}
    missing = sorted(expected_wall_tags - observed_tags)
    if missing:
        raise BuildError(f"{case_id}: surface.csv omits configured wall tags: {', '.join(missing)}")
    if case_id.startswith("naca"):
        y = np.asarray([float(row["y"]) for row in surface])
        if not (np.any(y > 0.0) and np.any(y < 0.0)):
            raise BuildError(f"{case_id}: NACA surface output must contain both upper and lower wall samples")


def _require_metadata_claims(case_id: str, metadata: dict[str, Any]) -> None:
    for name in METHOD_METADATA_FIELDS:
        if not str(metadata.get(name, "")).strip():
            raise BuildError(f"{case_id}: metadata must state {name}")
    if metadata.get("equation_set") != "compressible_navier_stokes_2d":
        raise BuildError(f"{case_id}: report supports the required compressible Navier--Stokes equation set only")
    if int(_number(metadata.get("spatial_order_claimed"), f"{case_id} spatial_order_claimed")) < 2:
        raise BuildError(f"{case_id}: production report requires claimed spatial order >= 2")
    if metadata.get("full_state_replication_during_iterations") is not False:
        raise BuildError(f"{case_id}: metadata must explicitly declare no full-state replication during iterations")
    if metadata.get("full_mesh_replication_during_iterations") is not False:
        raise BuildError(f"{case_id}: metadata must explicitly declare no full-mesh replication during iterations")
    if "metis" not in str(metadata["partitioner"]).lower():
        raise BuildError(f"{case_id}: partitioner must identify METIS or ParMETIS")
    prohibited = ("allgather", "allgatherv", "replicat")
    if any(token in str(metadata["halo_exchange"]).lower() for token in prohibited):
        raise BuildError(f"{case_id}: halo_exchange suggests prohibited state replication")


def _load_case_dir(directory: Path, case_id: str, case_input: dict[str, Any]) -> CaseData:
    if not directory.is_dir():
        raise BuildError(f"missing case directory: {directory}")
    metadata = _read_json(directory / "metadata.json")
    status = _read_json(directory / "run_status.json")
    if metadata.get("case_id") != case_id or status.get("case_id") != case_id:
        raise BuildError(f"{directory}: metadata/run status case_id must both equal directory name")
    if metadata.get("completed") is not True:
        raise BuildError(f"{case_id}: metadata completed is not true")
    if metadata.get("convergence_status") not in ALLOWED_STATUS or status.get("convergence_status") not in ALLOWED_STATUS:
        raise BuildError(f"{case_id}: failed or non-final status cannot be reported as success")
    if metadata["convergence_status"] != status["convergence_status"]:
        raise BuildError(f"{case_id}: metadata and run status convergence_status disagree")
    _require_metadata_claims(case_id, metadata)
    if int(_number(metadata.get("mpi_ranks"), f"{case_id} metadata mpi_ranks")) != int(_number(status.get("mpi_ranks"), f"{case_id} run status mpi_ranks")):
        raise BuildError(f"{case_id}: metadata and run status mpi_ranks disagree")
    if int(_number(status.get("final_step"), f"{case_id} final_step")) <= 0:
        raise BuildError(f"{case_id}: final_step must be positive")
    try:
        residuals, forces, surface = read_case_tables(directory)
        field_path = final_field_path(directory)
    except PlotError as exc:
        raise BuildError(str(exc)) from exc
    final_step = int(_number(status["final_step"], f"{case_id} final_step"))
    final_time = _number(status["final_physical_time"], f"{case_id} final_physical_time")
    if int(float(forces[-1]["step"])) != final_step:
        raise BuildError(f"{case_id}: final forces row does not match run_status final_step")
    if not math.isclose(float(forces[-1]["physical_time"]), final_time, rel_tol=0.0, abs_tol=1.0e-10):
        raise BuildError(f"{case_id}: final forces row physical_time does not match run_status")
    _validate_force_evidence(case_id, case_input, forces)
    _validate_surface_evidence(case_id, case_input, surface)
    _validate_production_evidence(case_id, case_input, metadata, status, residuals, forces)
    partition_rows = _read_partition_diagnostics(directory)
    ranks = int(_number(status["mpi_ranks"], f"{case_id} mpi_ranks"))
    _validate_partition_evidence(case_id, metadata, partition_rows, ranks)
    return CaseData(case_id, directory, metadata, status, residuals, forces, surface, field_path,
                    case_input, partition_rows)


def _load_case(results_root: Path, case_id: str, case_input: dict[str, Any]) -> CaseData:
    return _load_case_dir(results_root / case_id, case_id, case_input)


def load_complete_results(results_root: Path, case_inputs: dict[str, dict[str, Any]]) -> list[CaseData]:
    if not results_root.is_dir():
        raise BuildError(f"missing results root: {results_root}")
    return [_load_case(results_root, case_id, case_inputs[case_id]) for case_id in REQUIRED_CASES]


def _residual_reduction(case: CaseData) -> float:
    initial = float(case.residuals[0]["residual_l2"])
    final = float(case.residuals[-1]["residual_l2"])
    if initial <= 0.0 or final <= 0.0:
        raise BuildError(f"{case.case_id}: residual history must have positive L2 values")
    return math.log10(initial / final)


def _linf_residual_reduction(case: CaseData) -> float:
    initial = float(case.residuals[0]["residual_linf"])
    final = float(case.residuals[-1]["residual_linf"])
    if initial <= 0.0 or final <= 0.0:
        raise BuildError(f"{case.case_id}: residual history must have positive Linf values")
    return math.log10(initial / final)


def _steady_terminal_plateau(residuals: list[dict[str, float | str]],
                             forces: list[dict[str, float | str]]) -> bool:
    """A conservative fallback when the supplied residual target is not reached."""
    if len(residuals) < STEADY_MIN_HISTORY_ROWS or len(forces) < STEADY_TERMINAL_WINDOW:
        return False
    residual_tail = np.asarray([float(row["residual_l2"]) for row in residuals[-STEADY_TERMINAL_WINDOW:]])
    linf_tail = np.asarray([float(row["residual_linf"]) for row in residuals[-STEADY_TERMINAL_WINDOW:]])
    drag_tail = np.asarray([float(row["cd"]) for row in forces[-STEADY_TERMINAL_WINDOW:]])
    lift_tail = np.asarray([float(row["cl"]) for row in forces[-STEADY_TERMINAL_WINDOW:]])
    if not np.all(residual_tail > 0.0) or not np.all(linf_tail > 0.0):
        return False
    residual_spread = float(np.ptp(np.log10(residual_tail)))
    linf_spread = float(np.ptp(np.log10(linf_tail)))
    force_scale = max(float(np.max(np.abs(drag_tail))), float(np.max(np.abs(lift_tail))), 1.0e-8)
    force_spread = max(float(np.ptp(drag_tail)), float(np.ptp(lift_tail))) / force_scale
    return residual_spread <= 0.15 and linf_spread <= 0.15 and force_spread <= 0.01


def _validate_transient_evidence(case_id: str, controls: dict[str, Any], metadata: dict,
                                 status: dict, residuals: list[dict[str, float | str]],
                                 forces: list[dict[str, float | str]]) -> None:
    dt = _number(controls["time_step"], f"{case_id} supplied time_step")
    final_time = _number(controls["final_time"], f"{case_id} supplied final_time")
    expected_steps = int(round(final_time / dt))
    if not math.isclose(_number(status["final_physical_time"], f"{case_id} final_physical_time"), final_time,
                        rel_tol=0.0, abs_tol=1.0e-10):
        raise BuildError(f"{case_id}: final physical time must equal supplied production horizon")
    if int(_number(status["final_step"], f"{case_id} final_step")) != expected_steps:
        raise BuildError(f"{case_id}: final step must equal supplied dt/final-time horizon")
    if metadata.get("true_bdf2_inner_loop") is not True or "bdf2" not in str(metadata.get("time_integrator", "")).lower():
        raise BuildError(f"{case_id}: this report supports the submitted true BDF2 production path")
    if status.get("convergence_status") != "statistically_periodic":
        raise BuildError(f"{case_id}: Re200 production output must be statistically_periodic")
    supplied_min = int(_number(controls["min_inner_iterations"], f"{case_id} supplied minimum inner iterations"))
    supplied_max = int(_number(controls["max_inner_iterations"], f"{case_id} supplied maximum inner iterations"))
    supplied_target = _number(controls["inner_residual_reduction_target"], f"{case_id} supplied inner target")
    configured_min = int(_number(metadata.get("min_inner_iterations"), f"{case_id} configured minimum inner iterations"))
    configured_max = int(_number(metadata.get("max_inner_iterations"), f"{case_id} configured maximum inner iterations"))
    configured_target = _number(metadata.get("inner_residual_reduction_target"), f"{case_id} configured inner target")
    if configured_min < supplied_min or configured_max > supplied_max or configured_min > configured_max:
        raise BuildError(f"{case_id}: configured transient inner bounds are looser than supplied production controls")
    if configured_target > supplied_target:
        raise BuildError(f"{case_id}: configured transient inner target is looser than supplied production control")
    if len(forces) != expected_steps:
        raise BuildError(f"{case_id}: forces.csv must contain one accepted sample for every physical step")
    for expected_step, row in enumerate(forces, start=1):
        if int(float(row["step"])) != expected_step or not math.isclose(float(row["physical_time"]), expected_step * dt,
                                                                           rel_tol=0.0, abs_tol=1.0e-10):
            raise BuildError(f"{case_id}: force history is not sampled at the supplied physical-time cadence")
    residuals_by_step: dict[int, list[dict[str, float | str]]] = {}
    supplied_cfl = _number(controls["cfl_max"], f"{case_id} supplied transient CFL")
    for row in residuals:
        step = int(float(row["step"]))
        if step < 1 or step > expected_steps:
            raise BuildError(f"{case_id}: residual history has an invalid physical step")
        if not math.isclose(float(row["dt"]), dt, rel_tol=0.0, abs_tol=1.0e-12):
            raise BuildError(f"{case_id}: residual dt differs from the supplied production dt")
        if not math.isclose(float(row["physical_time"]), step * dt, rel_tol=0.0, abs_tol=1.0e-10):
            raise BuildError(f"{case_id}: residual physical time differs from step*dt")
        if not math.isclose(float(row["cfl"]), supplied_cfl, rel_tol=0.0, abs_tol=1.0e-12):
            raise BuildError(f"{case_id}: transient pseudo-time CFL differs from the supplied fixed value")
        residuals_by_step.setdefault(step, []).append(row)
    if len(residuals_by_step) != expected_steps:
        raise BuildError(f"{case_id}: residual history must cover every accepted physical step")
    observed_iterations: list[int] = []
    final_ratios: list[float] = []
    for step in range(1, expected_steps + 1):
        rows = residuals_by_step[step]
        inner_indices = [int(float(row["inner_iter"])) for row in rows]
        if inner_indices != list(range(1, len(rows) + 1)):
            raise BuildError(f"{case_id}: physical step {step} must contain a contiguous inner-iteration trace")
        used = inner_indices[-1]
        if used < configured_min or used > configured_max:
            raise BuildError(f"{case_id}: physical step {step} violates configured inner-iteration bounds")
        first = float(rows[0]["residual_l2"])
        final = float(rows[-1]["residual_l2"])
        if first <= 0.0 or final < 0.0:
            raise BuildError(f"{case_id}: physical step {step} has an invalid inner residual")
        ratio = final / first
        if ratio > configured_target * (1.0 + 1.0e-6):
            raise BuildError(f"{case_id}: physical step {step} misses the configured total-residual target")
        observed_iterations.append(used)
        final_ratios.append(ratio)
    observed_min = int(_number(metadata.get("observed_min_inner_iterations"), "observed_min_inner_iterations"))
    observed_max = int(_number(metadata.get("observed_max_inner_iterations"), "observed_max_inner_iterations"))
    observed_mean = _number(metadata.get("observed_mean_inner_iterations"), "observed_mean_inner_iterations")
    if observed_min != min(observed_iterations) or observed_max != max(observed_iterations):
        raise BuildError(f"{case_id}: stored transient inner extrema do not match residuals.csv")
    if not math.isclose(observed_mean, float(np.mean(observed_iterations)), rel_tol=1.0e-10, abs_tol=1.0e-10):
        raise BuildError(f"{case_id}: stored transient mean inner iterations do not match residuals.csv")
    if int(_number(metadata.get("inner_target_misses"), "inner_target_misses")) != 0:
        raise BuildError(f"{case_id}: completed transient production output may not contain inner-target misses")
    if not math.isclose(_number(metadata.get("inner_target_converged_fraction"), "inner_target_converged_fraction"),
                        1.0, rel_tol=0.0, abs_tol=1.0e-12):
        raise BuildError(f"{case_id}: every accepted transient step must meet the inner target")
    if not math.isclose(_number(metadata.get("last_inner_residual_ratio"), "last_inner_residual_ratio"),
                        final_ratios[-1], rel_tol=1.0e-4, abs_tol=1.0e-12):
        raise BuildError(f"{case_id}: stored final inner ratio does not match residuals.csv")


def _validate_production_evidence(case_id: str, case_input: dict[str, Any], metadata: dict,
                                  status: dict, residuals: list[dict[str, float | str]],
                                  forces: list[dict[str, float | str]]) -> None:
    controls = case_input["run_control"]
    if controls["type"] == "transient":
        _validate_transient_evidence(case_id, controls, metadata, status, residuals, forces)
        return
    if len(residuals) < STEADY_MIN_HISTORY_ROWS or len(forces) < STEADY_MIN_HISTORY_ROWS:
        raise BuildError(f"{case_id}: steady production evidence needs at least {STEADY_MIN_HISTORY_ROWS} residual and force rows")
    final_step = int(_number(status["final_step"], f"{case_id} final_step"))
    if len(residuals) != final_step or len(forces) != final_step:
        raise BuildError(f"{case_id}: cadence-1 steady histories must contain exactly one row per final step")
    expected_steps = list(range(1, final_step + 1))
    if ([int(float(row["step"])) for row in residuals] != expected_steps or
            [int(float(row["step"])) for row in forces] != expected_steps):
        raise BuildError(f"{case_id}: steady histories must be sequential through the final step")
    reduction = math.log10(float(residuals[0]["residual_l2"]) / float(residuals[-1]["residual_l2"]))
    linf_reduction = math.log10(float(residuals[0]["residual_linf"]) /
                                float(residuals[-1]["residual_linf"]))
    target = _number(controls["residual_reduction_target"], f"{case_id} residual target")
    if ((reduction + 1.0e-12 < target or linf_reduction + 1.0e-12 < target) and
            not _steady_terminal_plateau(residuals, forces)):
        raise BuildError(
            f"{case_id}: steady run misses L2/Linf residual target without a stable terminal plateau")


def _tail_rows(case: CaseData) -> list[dict[str, float | str]]:
    if "re200" not in case.case_id:
        return case.forces[-1:]
    times = np.asarray([float(row["physical_time"]) for row in case.forces])
    if len(times) < 8 or not np.all(np.diff(times) > 0.0):
        raise BuildError(f"{case.case_id}: force history needs at least eight strictly increasing physical-time samples")
    start = times[0] + 0.8 * (times[-1] - times[0])
    tail = [row for row in case.forces if float(row["physical_time"]) >= start]
    if len(tail) < RE200_MIN_SPECTRUM_SAMPLES:
        raise BuildError(f"{case.case_id}: post-transient force window is too short for shedding analysis")
    return tail


def case_analysis(case: CaseData) -> dict[str, object]:
    final = case.forces[-1]
    analysis: dict[str, object] = {
        "computed_residual_reduction_orders": _residual_reduction(case),
        "computed_linf_residual_reduction_orders": _linf_residual_reduction(case),
        "reported_residual_reduction_orders": _number(case.status["residual_reduction_orders"], f"{case.case_id} residual_reduction_orders"),
        "final_cd": float(final["cd"]),
        "final_cl": float(final["cl"]),
        "final_cmz": float(final["cmz"]),
        "final_pressure_drag": float(final["pressure_drag"]),
        "final_viscous_drag": float(final["viscous_drag"]),
        "observed_cfl_range": [float(min(float(row["cfl"]) for row in case.residuals)),
                               float(max(float(row["cfl"]) for row in case.residuals))],
        "observed_dt_range": [float(min(float(row["dt"]) for row in case.residuals)),
                              float(max(float(row["dt"]) for row in case.residuals))],
    }
    if "re200" in case.case_id:
        tail = _tail_rows(case)
        cd = np.asarray([float(row["cd"]) for row in tail])
        cl = np.asarray([float(row["cl"]) for row in tail])
        try:
            spectrum = lift_spectrum(np.asarray([float(row["physical_time"]) for row in tail]), cl)
        except PlotError as exc:
            raise BuildError(f"{case.case_id}: cannot form traceable lift spectrum: {exc}") from exc
        frequency = float(spectrum["dominant_frequency"])
        peak_ratio = float(spectrum["peak_to_second_ratio"])
        if peak_ratio < 1.2:
            raise BuildError(f"{case.case_id}: lift spectrum has no sufficiently distinct dominant peak")
        analysis.update({
            "post_transient_window": [float(tail[0]["physical_time"]), float(tail[-1]["physical_time"])],
            "mean_cd": float(np.mean(cd)),
            "mean_cl": float(np.mean(cl)),
            "lift_amplitude": float(0.5 * np.ptp(cl)),
            "dominant_lift_frequency": frequency,
            "strouhal_number": frequency,  # U_inf=L_ref=1 in the supplied case input.
            "spectrum_sample_dt": float(spectrum["sample_dt"]),
            "spectrum_frequency_resolution": float(spectrum["frequency_resolution"]),
            "spectrum_peak_to_second_ratio": peak_ratio,
        })
    return analysis


def parameter_deviations(case: CaseData) -> list[str]:
    """Describe observed controls against the immutable supplied case controls."""
    controls = case.case_input["run_control"]
    analysis = case_analysis(case)
    deviations: list[str] = []
    observed_cfl = analysis["observed_cfl_range"]
    if "cfl_initial" in controls and float(observed_cfl[0]) < float(controls["cfl_initial"]) - 1.0e-12:
        deviations.append("observed residual CFL falls below the supplied initial CFL")
    if "cfl_max" in controls and float(observed_cfl[1]) > float(controls["cfl_max"]) + 1.0e-12:
        deviations.append("observed residual CFL exceeds the supplied CFL cap")
    if (controls.get("type") == "steady" and "cfl_max" in controls and
            float(observed_cfl[1]) < float(controls["cfl_max"]) -
            1.0e-12 * max(float(controls["cfl_max"]), 1.0)):
        deviations.append(
            f"safeguarded terminal CFL {float(observed_cfl[1]):g} is below the supplied cap {float(controls['cfl_max']):g}")
    if (controls.get("type") == "steady" and
            int(controls.get("pseudo_cfl_ramp_steps", 0)) > 0 and
            abs(float(observed_cfl[1]) - float(observed_cfl[0])) <= 1.0e-12):
        deviations.append(
            f"stationary observed CFL {float(observed_cfl[0]):g} does not demonstrate the supplied ramp")
    if controls.get("type") == "transient":
        if case.metadata.get("true_bdf2_inner_loop") is not True or "bdf2" not in str(case.metadata.get("time_integrator", "")).lower():
            deviations.append("metadata does not identify a true BDF2 physical-time inner loop")
        if float(case.status["final_physical_time"]) < float(controls["final_time"]) - 1.0e-10:
            deviations.append("final physical time is shorter than the supplied production horizon")
        if int(float(case.status["final_step"])) < round(float(controls["final_time"]) / float(controls["time_step"])):
            deviations.append("final step is shorter than the supplied time-step horizon")
        if _number(case.metadata.get("inner_residual_reduction_target"), "inner_residual_reduction_target") > float(controls["inner_residual_reduction_target"]):
            deviations.append("inner residual target is looser than the supplied target")
        if int(_number(case.metadata.get("min_inner_iterations"), "min_inner_iterations")) < int(controls["min_inner_iterations"]):
            deviations.append("minimum inner-iteration setting is lower than supplied")
        if int(_number(case.metadata.get("max_inner_iterations"), "max_inner_iterations")) > int(controls["max_inner_iterations"]):
            deviations.append("maximum inner-iteration setting is looser than supplied")
    return deviations


def load_rank_validation(rank_directories: tuple[Path, ...], case_inputs: dict[str, dict[str, Any]]) -> dict[str, list[CaseData]]:
    if not rank_directories:
        raise BuildError("rank-count validation is required: provide --rank-results case directories")
    groups: dict[str, list[CaseData]] = {}
    for directory in rank_directories:
        metadata = _read_json(directory / "metadata.json")
        case_id = metadata.get("case_id")
        if case_id not in case_inputs:
            raise BuildError(f"{directory}: rank-validation metadata has an unknown case_id")
        groups.setdefault(str(case_id), []).append(_load_case_dir(directory, str(case_id), case_inputs[str(case_id)]))
    qualifying: dict[str, list[CaseData]] = {}
    for case_id, runs in groups.items():
        ranks = [int(_number(run.status["mpi_ranks"], f"{case_id} rank")) for run in runs]
        if len(set(ranks)) != len(ranks):
            raise BuildError(f"{case_id}: rank validation contains duplicate rank counts")
        if len(runs) >= 2 and 8 in ranks:
            qualifying[case_id] = sorted(runs, key=lambda run: int(run.status["mpi_ranks"]))
    if not any(case_id.startswith("naca") for case_id in qualifying):
        raise BuildError("rank validation needs at least one NACA case with two rank counts including np=8")
    if not any(case_id.startswith("cylinder") for case_id in qualifying):
        raise BuildError("rank validation needs at least one cylinder case with two rank counts including np=8")
    for case_id, runs in qualifying.items():
        baseline = runs[0]
        baseline_cd = float(baseline.forces[-1]["cd"])
        baseline_cl = float(baseline.forces[-1]["cl"])
        baseline_l2 = _residual_reduction(baseline)
        baseline_linf = _linf_residual_reduction(baseline)
        for run in runs[1:]:
            if run.status["convergence_status"] != baseline.status["convergence_status"]:
                raise BuildError(f"{case_id}: rank-validation convergence statuses disagree")
            cd_tolerance = max(RANK_CD_ABSOLUTE_TOLERANCE,
                               RANK_CD_RELATIVE_TOLERANCE * abs(baseline_cd))
            if abs(float(run.forces[-1]["cd"]) - baseline_cd) > cd_tolerance:
                raise BuildError(f"{case_id}: rank-validation drag differs beyond tolerance")
            if abs(float(run.forces[-1]["cl"]) - baseline_cl) > RANK_CL_ABSOLUTE_TOLERANCE:
                raise BuildError(f"{case_id}: rank-validation lift differs beyond tolerance")
            if abs(_residual_reduction(run) - baseline_l2) > RANK_RESIDUAL_ORDER_TOLERANCE:
                raise BuildError(f"{case_id}: rank-validation L2 reduction differs beyond tolerance")
            if abs(_linf_residual_reduction(run) - baseline_linf) > RANK_RESIDUAL_ORDER_TOLERANCE:
                raise BuildError(f"{case_id}: rank-validation Linf reduction differs beyond tolerance")
    return qualifying


def _native_scalar(source: dict[str, np.ndarray], names: tuple[str, ...],
                   component: int = 0) -> np.ndarray | None:
    wanted = {_normalise(name) for name in names}
    for name, values in source.items():
        if _normalise(name) not in wanted:
            continue
        if values.ndim != 2 or values.shape[1] <= component:
            raise PlotError(f"field {name!r} lacks component {component}")
        return values[:, component]
    return None


def _native_primitive_fields(mesh: Any) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, str]:
    """Return unmodified primitive tuples from one VTK data association.

    Cell data are preferred for this cell-centred solver. Point conversion in
    ``plot_results._field`` is intentionally reserved for visualization and
    must never alter extrema used by a physical sanity gate.
    """
    for association, source in (("cell_data", mesh.cell_data), ("point_data", mesh.point_data)):
        density = _native_scalar(source, ("density", "rho"))
        pressure = _native_scalar(source, ("pressure", "p"))
        velocity_x: np.ndarray | None = None
        velocity_y: np.ndarray | None = None
        for name, values in source.items():
            if _normalise(name) in {"velocity", "vel"} and values.ndim == 2 and values.shape[1] >= 2:
                velocity_x, velocity_y = values[:, 0], values[:, 1]
                break
        if velocity_x is None:
            velocity_x = _native_scalar(source, ("u", "velocityx", "velx"))
            velocity_y = _native_scalar(source, ("v", "velocityy", "vely"))
        if density is None or pressure is None or velocity_x is None or velocity_y is None:
            continue
        lengths = {len(density), len(pressure), len(velocity_x), len(velocity_y)}
        if len(lengths) != 1:
            raise PlotError(f"{association} primitive fields have mismatched tuple counts")
        return density, pressure, velocity_x, velocity_y, association
    raise PlotError("density, pressure, and both velocity components must share one field-data association")


def _sanity_for_case(case: CaseData) -> dict:
    """Compute transparent checks from actual submitted values; never fabricate a pass."""
    try:
        mesh = read_field(case.field_path)
        density, pressure, native_u, native_v, association = _native_primitive_fields(mesh)
    except PlotError as exc:
        raise BuildError(f"{case.case_id}: cannot perform field sanity checks: {exc}") from exc
    force_cd = np.asarray([float(row["cd"]) for row in case.forces])
    force_cl = np.asarray([float(row["cl"]) for row in case.forces])
    cp = np.asarray([float(row["cp"]) for row in case.surface])
    wall_u = np.asarray([float(row["u"]) for row in case.surface])
    wall_v = np.asarray([float(row["v"]) for row in case.surface])
    normal_velocity = np.asarray([float(row["nx"]) * float(row["u"]) + float(row["ny"]) * float(row["v"]) for row in case.surface])
    checks: dict[str, object] = {
        "case_id": case.case_id,
        "status_from_solver": case.status["convergence_status"],
        "positive_density": bool(np.all(density > 0.0)),
        "min_density": float(np.min(density)),
        "positive_pressure": bool(np.all(pressure > 0.0)),
        "min_pressure": float(np.min(pressure)),
        "native_field_association": association,
        "surface_cp_varies": bool(np.ptp(cp) > 1.0e-12),
        "cp_range": [float(np.min(cp)), float(np.max(cp))],
        "computed_residual_reduction_orders": case_analysis(case)["computed_residual_reduction_orders"],
        "computed_linf_residual_reduction_orders":
            case_analysis(case)["computed_linf_residual_reduction_orders"],
        "parameter_deviations": parameter_deviations(case),
    }
    if "inviscid" in case.case_id:
        viscous = max(abs(float(case.forces[-1]["viscous_drag"])), abs(float(case.forces[-1]["viscous_lift"])))
        gamma = _number(case.case_input["gas"]["gamma"], "gamma")
        freestream = case.case_input["freestream"]
        freestream_enthalpy = (
            gamma / (gamma - 1.0) *
            _number(freestream["pressure"], "freestream pressure") /
            _number(freestream["rho"], "freestream density") +
            0.5 * _number(freestream["velocity_magnitude"], "freestream velocity") ** 2
        )
        total_enthalpy = (gamma / (gamma - 1.0) * pressure / density +
                          0.5 * (native_u * native_u + native_v * native_v))
        enthalpy_error = np.abs(total_enthalpy / freestream_enthalpy - 1.0)
        low_state = ((density < 0.1 * _number(freestream["rho"], "freestream density")) &
                     (pressure < 0.1 * _number(freestream["pressure"], "freestream pressure")))
        checks.update({
            # Surface CSV uses decimal text, so allow a small roundoff margin
            # over the exact projected boundary value.
            "slip_wall_normal_velocity_negligible": bool(np.max(np.abs(normal_velocity)) <= 2.0e-6),
            "max_abs_surface_normal_velocity": float(np.max(np.abs(normal_velocity))),
            "viscous_force_negligible": bool(viscous <= 1.0e-8),
            "max_abs_final_viscous_force": viscous,
            "symmetric_lift_near_zero": bool(abs(float(case.forces[-1]["cl"])) <= 1.0e-4),
            "inviscid_total_enthalpy_p99_within_5pct": bool(
                np.percentile(enthalpy_error, 99.0) <= INVISCID_H0_P99_LIMIT),
            "inviscid_total_enthalpy_max_within_10pct": bool(
                np.max(enthalpy_error) <= INVISCID_H0_MAX_LIMIT),
            "max_relative_total_enthalpy_error": float(np.max(enthalpy_error)),
            "p99_relative_total_enthalpy_error": float(np.percentile(enthalpy_error, 99.0)),
            "joint_low_density_pressure_absent": bool(np.count_nonzero(low_state) == 0),
            "joint_low_density_pressure_points": int(np.count_nonzero(low_state)),
        })
    else:
        speed = np.hypot(wall_u, wall_v)
        cf = np.asarray([float(row["cf"]) for row in case.surface])
        checks.update({
            "no_slip_surface_velocity_negligible": bool(np.max(speed) <= 1.0e-6),
            "max_surface_speed": float(np.max(speed)),
            "skin_friction_evidence": bool(np.max(np.abs(cf)) > 1.0e-12),
        })
    if "cylinder" in case.case_id:
        checks.update({"positive_mean_drag": bool(np.mean(force_cd) > 0.0), "mean_drag": float(np.mean(force_cd))})
    if "re200" in case.case_id:
        tail_lift = np.asarray([float(row["cl"]) for row in _tail_rows(case)])
        checks.update({
            "unsteady_lift_variation": bool(np.ptp(tail_lift) >= 2.0e-3),
            "lift_range": [float(np.min(force_cl)), float(np.max(force_cl))],
            "post_transient_lift_amplitude": float(0.5 * np.ptp(tail_lift)),
            "velocity_present_for_wake": bool(np.isfinite(native_u).all() and np.isfinite(native_v).all()),
            "final_physical_time_at_least_300": bool(_number(case.status.get("final_physical_time"), "final_physical_time") >= 300.0),
            "true_bdf2_inner_loop": case.metadata.get("true_bdf2_inner_loop") is True,
            "inner_target_converged_fraction_at_least_095": bool(_number(case.metadata.get("inner_target_converged_fraction"), "inner_target_converged_fraction") >= 0.95),
            "inner_statistics_within_requested_bounds": bool(
                _number(case.metadata.get("observed_min_inner_iterations"), "observed_min_inner_iterations") >= _number(case.metadata.get("min_inner_iterations"), "min_inner_iterations") and
                _number(case.metadata.get("observed_max_inner_iterations"), "observed_max_inner_iterations") <= _number(case.metadata.get("max_inner_iterations"), "max_inner_iterations")),
        })
    return checks


def build_sanity_checks(cases: list[CaseData]) -> dict:
    result = {"schema_version": 1, "generated_from": "submitted solver output only", "cases": []}
    for case in cases:
        checks = _sanity_for_case(case)
        result["cases"].append(checks)
        failed = [key for key, value in checks.items() if isinstance(value, bool) and not value]
        if failed:
            raise BuildError(f"{case.case_id}: sanity checks failed: {', '.join(failed)}")
    return result


def _latex(value: object) -> str:
    replacements = {"\\": r"\textbackslash{}", "&": r"\&", "%": r"\%", "_": r"\_",
                    "#": r"\#", "{": r"\{", "}": r"\}", "~": r"\textasciitilde{}",
                    "^": r"\textasciicircum{}"}
    return "".join(replacements.get(character, character) for character in str(value))


def _fmt(value: object, digits: int = 5) -> str:
    return f"{_number(value, 'report value'):.{digits}g}"


def _partition_summary(case: CaseData) -> dict[str, float | int]:
    owned = np.asarray([int(row["num_cells_owned"]) for row in case.partition_rows], dtype=float)
    ghost = np.asarray([int(row["num_cells_ghost"]) for row in case.partition_rows], dtype=float)
    neighbors = np.asarray([int(row["num_neighbor_ranks"]) for row in case.partition_rows], dtype=float)
    send = np.asarray([sum(int(value) for value in str(row["send_cells"]).split(";") if value)
                       for row in case.partition_rows], dtype=float)
    recv = np.asarray([sum(int(value) for value in str(row["recv_cells"]).split(";") if value)
                       for row in case.partition_rows], dtype=float)
    return {
        "owned_min": int(np.min(owned)), "owned_max": int(np.max(owned)),
        "ghost_min": int(np.min(ghost)), "ghost_max": int(np.max(ghost)),
        "neighbor_min": int(np.min(neighbors)), "neighbor_max": int(np.max(neighbors)),
        "load_balance": float(np.max(owned) / np.mean(owned)),
        "edge_cut": int(_number(case.metadata.get("partition_edge_cut", 0), "partition_edge_cut")),
        "neighbor_mean": float(np.mean(neighbors)),
        "send_mean": float(np.mean(send)),
        "recv_mean": float(np.mean(recv)),
    }


def _figure_label(record: dict[str, str]) -> str:
    return f"fig:{record['case_id']}-{record['variable']}"


def _figure_block(record: dict[str, str]) -> str:
    return (
        "\\begin{figure}[htbp]\n\\centering\n"
        f"\\includegraphics[width=0.86\\linewidth]{{figures/{_latex(record['figure_file'])}}}\n"
        f"\\caption{{{_latex(record['caption'])}}}\n"
        f"\\label{{{_figure_label(record)}}}\n"
        "\\end{figure}"
    )


def _supplied_control_text(case: CaseData) -> str:
    controls = case.case_input["run_control"]
    if controls["type"] == "transient":
        return (f"dt={_fmt(controls['time_step'])}; tf={_fmt(controls['final_time'])}; "
                f"I={controls['min_inner_iterations']}--{controls['max_inner_iterations']}; "
                f"r={_fmt(controls['inner_residual_reduction_target'])}")
    return (f"N={controls['max_steps']}; R={_fmt(controls['residual_reduction_target'])}; "
            f"CFL={_fmt(controls['cfl_initial'])}--{_fmt(controls['cfl_max'])}; "
            f"ramp={controls['pseudo_cfl_ramp_steps']}; I={controls['min_inner_iterations']}--{controls['max_inner_iterations']}; "
            f"r={_fmt(controls['inner_residual_reduction_target'])}")


def _actual_control_text(case: CaseData) -> str:
    analysis = case_analysis(case)
    text = (f"CFL={_fmt(analysis['observed_cfl_range'][0])}--{_fmt(analysis['observed_cfl_range'][1])}; "
            f"N={case.status['final_step']}; I="
            f"{case.metadata.get('observed_min_inner_iterations', 'not recorded')}--"
            f"{case.metadata.get('observed_max_inner_iterations', 'not recorded')}; r="
            f"{case.metadata.get('inner_residual_reduction_target', 'not recorded')}")
    if case.case_input["run_control"]["type"] == "transient":
        text += (f"; dt observed {_fmt(analysis['observed_dt_range'][0])}--"
                 f"{_fmt(analysis['observed_dt_range'][1])}; tf {case.status['final_physical_time']}")
    else:
        text += "; ramp=observed directly in residual CFL rows"
    return text


def _render_report(cases: list[CaseData], records: list[dict[str, str]],
                   rank_validation: dict[str, list[CaseData]]) -> str:
    """Render a source-backed technical report; all result numbers come from CSV/JSON."""
    records_by_case: dict[str, list[dict[str, str]]] = {case.case_id: [] for case in cases}
    for record in records:
        records_by_case[record["case_id"]].append(record)
    analyses = {case.case_id: case_analysis(case) for case in cases}
    status_rows = "\n".join(
        f"{_latex(case.case_id)} & {case.status['mpi_ranks']} & {case.status['final_step']} & "
        f"{_fmt(case.status['final_physical_time'])} & {_fmt(analyses[case.case_id]['computed_residual_reduction_orders'], 3)} & "
        f"{_fmt(analyses[case.case_id]['computed_linf_residual_reduction_orders'], 3)} & "
        f"{_fmt(case.status['wall_time_seconds'], 4)} & {_latex(case.status['convergence_status'])} \\\\" for case in cases
    )
    force_rows = "\n".join(
        f"{_latex(case.case_id)} & {_fmt(analyses[case.case_id]['final_cd'])} & {_fmt(analyses[case.case_id]['final_cl'])} & "
        f"{_fmt(analyses[case.case_id]['final_cmz'])} & {_fmt(analyses[case.case_id]['final_pressure_drag'])} & "
        f"{_fmt(analyses[case.case_id]['final_viscous_drag'])} \\\\" for case in cases
    )
    method_rows = "\n".join(
        f"{_latex(case.case_id)} & {_latex(case.metadata['inviscid_flux'])} & {_latex(case.metadata['viscous_flux'])} & "
        f"{_latex(case.metadata['time_integrator'])} & {_latex(case.metadata['implicit_solver'])} \\\\" for case in cases
    )
    mesh_rows = "\n".join(
        f"{_latex(case.case_id)} & {case.metadata['num_cells_global']} & {case.metadata['num_faces_global']} & "
        f"{_fmt(case.case_input['freestream']['mach'])} & {_latex(case.case_input['physics']['mode'])} & "
        f"{_latex(', '.join(sorted({str(row['tag']) for row in case.surface})))} \\\\" for case in cases
    )
    partition_rows = "\n".join(
        (lambda summary: f"{_latex(case.case_id)} & {case.status['mpi_ranks']} & "
         f"{summary['owned_min']}--{summary['owned_max']} & {summary['ghost_min']}--{summary['ghost_max']} & "
         f"{summary['neighbor_min']}--{summary['neighbor_max']} & {_fmt(summary['load_balance'], 4)} & {summary['edge_cut']} \\\\")( _partition_summary(case))
        for case in cases
    )
    controls_rows = "\n".join(
        f"{_latex(case.case_id)} & {_latex(_supplied_control_text(case))} & {_latex(_actual_control_text(case))} & "
        f"{_latex('; '.join(parameter_deviations(case)) if parameter_deviations(case) else 'none detected from stored evidence')} \\\\" for case in cases
    )
    rank_rows: list[str] = []
    for case_id, runs in sorted(rank_validation.items()):
        baseline = runs[0]
        base_cd, base_cl = float(baseline.forces[-1]["cd"]), float(baseline.forces[-1]["cl"])
        base_residual = _residual_reduction(baseline)
        for run in runs:
            cd_delta = abs(float(run.forces[-1]["cd"]) - base_cd)
            cl_delta = abs(float(run.forces[-1]["cl"]) - base_cl)
            summary = _partition_summary(run)
            residual_delta = abs(_residual_reduction(run) - base_residual)
            rank_rows.append(
                f"{_latex(case_id)} & {run.status['mpi_ranks']} & {_fmt(run.status['wall_time_seconds'], 4)} & "
                f"{_fmt(run.forces[-1]['cd'])} & {_fmt(run.forces[-1]['cl'])} & {_fmt(cd_delta, 3)} & {_fmt(cl_delta, 3)} & "
                f"{_fmt(residual_delta, 3)} & {_fmt(summary['load_balance'], 3)} & {_fmt(summary['neighbor_mean'], 3)} & "
                f"{_fmt(summary['send_mean'], 3)}/{_fmt(summary['recv_mean'], 3)} \\\\"
            )
    case_sections: list[str] = []
    for case in cases:
        by_variable = {record["variable"]: record for record in records_by_case[case.case_id]}
        required = ("residual", "force_coefficients", "surface_cp", "mach", "pressure")
        missing = [variable for variable in required if variable not in by_variable]
        if missing:
            raise BuildError(f"{case.case_id}: figure generator omitted {', '.join(missing)}")
        references = ", ".join(f"Figure~\\ref{{{_figure_label(by_variable[variable])}}}" for variable in required)
        analysis = analyses[case.case_id]
        result_text = (
            f"The submitted status is \\texttt{{{_latex(case.status['convergence_status'])}}}; the computed first-to-last "
            f"$L_2$/$L_\\infty$ residual reductions are "
            f"{_fmt(analysis['computed_residual_reduction_orders'], 3)}/"
            f"{_fmt(analysis['computed_linf_residual_reduction_orders'], 3)} orders. "
            f"The final force row gives $C_D={_fmt(analysis['final_cd'])}$, $C_L={_fmt(analysis['final_cl'])}$, and "
            f"$C_m={_fmt(analysis['final_cmz'])}$. {references} provide the residual, force, wall $C_p/C_f$, Mach, and pressure evidence."
        )
        if "re200" in case.case_id:
            if "vorticity" not in by_variable or "lift_spectrum" not in by_variable:
                raise BuildError(f"{case.case_id}: figure generator omitted vorticity or lift spectrum")
            result_text += (
                f" The final 20\\% physical-time window is {_fmt(analysis['post_transient_window'][0])}--"
                f"{_fmt(analysis['post_transient_window'][1])}; it gives mean $C_D={_fmt(analysis['mean_cd'])}$, "
                f"lift amplitude {_fmt(analysis['lift_amplitude'])}, and dominant lift frequency "
                f"{_fmt(analysis['dominant_lift_frequency']) if analysis['dominant_lift_frequency'] is not None else 'not resolved'} "
                f"(Strouhal number is identical because the supplied $U_\\infty=L_{{ref}}=1$). The native force cadence is "
                f"$\\Delta t={_fmt(analysis['spectrum_sample_dt'])}$, spectral resolution is "
                f"{_fmt(analysis['spectrum_frequency_resolution'])}, and peak/second magnitude ratio is "
                f"{_fmt(analysis['spectrum_peak_to_second_ratio'])}. Figure~\\ref{{{_figure_label(by_variable['vorticity'])}}} "
                f"is the post-transient wake visualization and Figure~\\ref{{{_figure_label(by_variable['lift_spectrum'])}}} "
                f"is the traceable lift spectrum."
            )
        case_sections.append(
            f"\\subsection{{{_latex(case.case_id)}}}\n{result_text}\n" +
            "\n".join(_figure_block(record) for record in records_by_case[case.case_id])
        )
    bdf2_case = next(case for case in cases if "re200" in case.case_id)
    bdf2 = bdf2_case.metadata
    return rf'''\documentclass[11pt]{{article}}
\usepackage[margin=1in]{{geometry}}
\usepackage{{amsmath,amssymb,graphicx,booktabs,hyperref,cleveref,tabularx}}
\title{{2-D Unstructured Compressible Navier--Stokes Solver Benchmark Report}}
\author{{Submitted solver team}}
\date{{\today}}
\begin{{document}}
\maketitle
\begin{{abstract}}
This report is generated from completed solver outputs only.  It covers all eight required cases, their submitted convergence status, force and residual evidence, field figures, MPI partition evidence, rank-count comparisons, and the cylinder Reynolds-200 shedding analysis.  The automation rejects failed, incomplete, or internally inconsistent report inputs; it does not independently establish physical correctness or solver provenance.
\end{{abstract}}
\section{{Introduction}}
The benchmark solves the two-dimensional NACA0012 and circular-cylinder cases with a cell-centred, unstructured finite-volume compressible-flow solver.  Required results are accepted here only when their submitted status is \texttt{{converged}} or \texttt{{statistically\_periodic}}.  Values in tables and figures are read from the submitted CSV, VTK, JSON, and partition-diagnostic files; no histories, fields, or convergence labels are synthesized by this script.  These checks establish output consistency, not independent physical validation or proof of solver provenance.
\section{{Governing equations and nondimensionalization}}
The conservative state is $\mathbf U=[\rho,\rho u,\rho v,\rho E]^T$ and the reported equation set is \texttt{{compressible\_navier\_stokes\_2d}}.  The discretized equation is
\[
\frac{{\partial\mathbf U}}{{\partial t}}+\frac{{\partial\mathbf F^i}}{{\partial x}}+\frac{{\partial\mathbf G^i}}{{\partial y}}=\frac{{\partial\mathbf F^v}}{{\partial x}}+\frac{{\partial\mathbf G^v}}{{\partial y}},
\quad
\mathbf F^i=\begin{{bmatrix}}\rho u\\\rho u^2+p\\\rho uv\\u(\rho E+p)\end{{bmatrix}},\quad
\mathbf G^i=\begin{{bmatrix}}\rho v\\\rho uv\\\rho v^2+p\\v(\rho E+p)\end{{bmatrix}}.
\]
For the calorically perfect gas, $p=(\gamma-1)\rho e$, $E=e+\tfrac12(u^2+v^2)$, and $a=\sqrt{{\gamma p/\rho}}$.  The supplied inputs use $\gamma=1.4$, $R=1$, and $Pr=0.72$; laminar cases use constant viscosity $\mu=\rho_\infty U_\infty L_{{ref}}/Re$.  With $q_\infty=\tfrac12\rho_\infty U_\infty^2$, the reported coefficients are $C_D=D/(q_\infty A_{{ref}})$, $C_L=L/(q_\infty A_{{ref}})$, and $C_m=M_z/(q_\infty A_{{ref}}L_{{ref}})$.

For viscous fluxes the Newtonian/Fourier model is $\tau_{{xx}}=2\mu u_x-\tfrac23\mu(u_x+v_y)$, $\tau_{{yy}}=2\mu v_y-\tfrac23\mu(u_x+v_y)$, $\tau_{{xy}}=\mu(u_y+v_x)$, and $\mathbf q=-\mu c_p\nabla T/Pr$.  The force split reports pressure traction separately from the tangential component of viscous traction; $C_f$ is based on this tangential traction rather than normal viscous traction.
\section{{Meshes, boundaries, and spatial discretization}}
Each residual is a sum of oriented face fluxes over an unstructured control volume, $R_i=\sum_{{f\in\partial\Omega_i}}(\widehat{{F}}^i_f-\widehat{{F}}^v_f)|S_f|$, with each face normal directed from the owner cell to its neighbour or exterior.  Interior and farfield faces use HLLC with an admissibility-checked Rusanov fallback.  The case inputs map CGNS boundary families to farfield plus either inviscid slip wall or viscous no-slip adiabatic wall.  Farfield faces use a characteristic exterior state.  Every stationary impermeable body face uses the exact inviscid flux $[0,pn_x,pn_y,0]^T$ rather than solving a reflected reconstructed wall Riemann problem; no-slip walls additionally impose $u=v=0$ and zero normal temperature gradient in the viscous flux.  Submitted surface data are declared as \texttt{{boundary\_value}} output, so wall velocities in the report are not conflated with adjacent cell-centre velocities.

Gradients are weighted least-squares reconstructions, face states are piecewise linear, and the production limiter is the submitted Barth--Jespersen method.  Face-state density/pressure positivity uses the submitted scaling/backtracking method.  The report does not infer any first-order fallback: its occurrence and trigger must be recorded in run notes or metadata before it can be claimed.

\begin{{table}}[htbp]\centering\scriptsize
\caption{{Submitted mesh/case evidence. Boundary tags are the tags present in submitted wall surface rows.}}\label{{tab:mesh}}
\begin{{tabularx}}{{\linewidth}}{{lrrrrX}}\toprule Case & cells & faces & Mach & mode & surface tags\\\midrule
{mesh_rows}
\bottomrule\end{{tabularx}}\end{{table}}

The production metadata identifies the reconstruction, limiter, and positivity method for every result: piecewise-linear reconstruction is applied to face states, the stated limiter controls oscillations, and the stated positivity control protects density and pressure.  The report does not infer an unused first-order fallback; any fallback must be described by the submitted metadata/notes.
\begin{{center}}\small\begin{{tabularx}}{{\linewidth}}{{lXX X X}}\toprule
Case & inviscid flux & viscous flux & time integrator & implicit solver\\\midrule
{method_rows}
\bottomrule\end{{tabularx}}\end{{center}}
\section{{Implicit steady march and true transient BDF2}}
Steady cases use the submitted local pseudo-time integrator and inner solver.  They retain the supplied startup CFL and ramp horizon while applying a documented conservative terminal cap; Anderson history is reset whenever the CFL changes.  Accelerated iterates must strictly reduce both global $L_2$ and $L_\infty$ residuals, and target convergence requires both norms plus a stable force tail.  The stationary-wall block uses the analytic pressure-flux Jacobian while its spectral radius remains in the pseudo-time mass.  Requested controls and detectable departures are listed in \cref{{tab:controls}}.  For the Re 200 cylinder, metadata identifies \texttt{{{_latex(bdf2['time_integrator'])}}}, with true BDF2 inner loop set to \texttt{{{_latex(bdf2['true_bdf2_inner_loop'])}}}.  The required outer physical-time loop holds $U^n$ and $U^{{n-1}}$ fixed while inner iterations solve for $U^{{n+1}}$, then advances the histories after accepted inner convergence.  Its submitted observed inner-iteration range is {_latex(bdf2.get('observed_min_inner_iterations'))}--{_latex(bdf2.get('observed_max_inner_iterations'))}, mean {_fmt(bdf2.get('observed_mean_inner_iterations', bdf2.get('typical_inner_iterations', 0)))}, target-miss count {_latex(bdf2.get('inner_target_misses'))}, converged-step fraction {_fmt(bdf2.get('inner_target_converged_fraction'))}, and last ratio {_fmt(bdf2.get('last_inner_residual_ratio'))}.
\begin{{table}}[htbp]\centering\scriptsize
\caption{{Supplied and observed controls.  Steady ramp details are explicitly marked unavailable when metadata does not store them.}}\label{{tab:controls}}
\begin{{tabularx}}{{\linewidth}}{{lXXX}}\toprule Case & supplied controls & actual stored evidence & detected deviation\\\midrule
{controls_rows}
\bottomrule\end{{tabularx}}\end{{table}}
\section{{MPI decomposition, halo exchange, and no replication}}
The metadata identifies the partitioner and halo exchange for every submitted run.  It explicitly declares both full-mesh and full-conservative-state replication during solver iterations false.  Partition diagnostics below are the actual rank-local owned/ghost counts, neighbour ranges, and edge-cut metadata, rather than a preprocessing-only estimate.  Halo communication is neighbour-scoped as named in metadata; residuals and forces are the submitted global histories.
\begin{{table}}[htbp]\centering\small
\caption{{Final-run partition evidence.  Load balance is maximum owned cells divided by mean owned cells.}}\label{{tab:partition}}
\begin{{tabular}}{{lrrrrrr}}\toprule Case & ranks & owned min--max & ghost min--max & neighbours min--max & load balance & edge cut\\\midrule
{partition_rows}
\bottomrule\end{{tabular}}\end{{table}}
\section{{Output reproducibility and sanity checks}}
The generated \texttt{{run\_manifest.csv}} records both production and rank-validation commands, source directories, ranks, wall times, final steps, and statuses.  \texttt{{figure\_manifest.csv}} maps every figure to the actual CSV or field file and named variable.  \texttt{{sanity\_checks.json}} records contract-derived positivity, wall-condition, force, pressure-coefficient, and Re 200 wake checks.  Regeneration is documented with this repository's report tool; the TeX source is ready for \texttt{{latexmk}}.
\section{{Results}}
\subsection{{Run status and final forces}}
\begin{{table}}[htbp]\centering\small
\caption{{Submitted run status.  Both residual reductions are recomputed from the first and final rows.}}\label{{tab:status}}
\begin{{tabular}}{{lrrrrrrl}}\toprule Case & ranks & steps & time & $L_2$ orders & $L_\infty$ orders & wall s & status\\\midrule
{status_rows}
\bottomrule\end{{tabular}}\end{{table}}
\begin{{table}}[htbp]\centering\small
\caption{{Final force coefficients from each submitted force history.}}\label{{tab:forces}}
\begin{{tabular}}{{lrrrrr}}\toprule Case & $C_D$ & $C_L$ & $C_m$ & pressure drag & viscous drag\\\midrule
{force_rows}
\bottomrule\end{{tabular}}\end{{table}}
{chr(10).join(case_sections)}
\section{{Parallel validation}}
The following independent completed result packages compare final forces, residual reduction, rank-local load balance, neighbour count, and mean halo send/receive cells across MPI rank counts.  Absolute differences are measured against the smallest submitted rank count for the same case.  Timing is wall-clock time; the available CSV diagnostics quantify topology and payload but do not independently measure communication time.
\begin{{table}}[htbp]\centering\scriptsize
\caption{{Rank-count consistency, residual reduction difference, and rank-local communication evidence.}}\label{{tab:ranks}}
\resizebox{{\linewidth}}{{!}}{{\begin{{tabular}}{{lrrrrrrrrrr}}\toprule Case & ranks & wall s & $C_D$ & $C_L$ & $|\Delta C_D|$ & $|\Delta C_L|$ & $|\Delta R|$ & balance & nbr mean & send/recv mean\\\midrule
{chr(10).join(rank_rows)}
\bottomrule\end{{tabular}}}}\end{{table}}
\section{{Limitations and failure analysis}}
This report is an evidence renderer, not a numerical adjudicator.  It rejects failed statuses and failed contract-derived sanity checks, but it cannot prove grid convergence, model-form accuracy, or source-code behavior from output alone.  Any remaining limitations are retained verbatim in the submitted run-manifest notes and should be evaluated alongside the residual, force, surface, field, and rank-comparison evidence above.
\end{{document}}
'''


def _write_csv(path: Path, columns: tuple[str, ...], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(columns))
        writer.writeheader()
        writer.writerows(rows)


def build_report(results_root: Path, output_dir: Path, *,
                 rank_results: tuple[Path, ...] = (),
                 case_inputs_dir: Path | None = None) -> None:
    """Validate production/rank evidence, then create reproducible report artifacts."""
    case_inputs = load_case_inputs(case_inputs_dir or default_case_inputs_dir())
    cases = load_complete_results(results_root, case_inputs)
    rank_validation = load_rank_validation(rank_results, case_inputs)
    sanity = build_sanity_checks(cases)
    if output_dir.exists():
        raise BuildError(f"refusing to overwrite existing report directory: {output_dir}")
    output_dir.mkdir(parents=True)
    figures = output_dir / "figures"
    records: list[dict[str, str]] = []
    try:
        for case in cases:
            records.extend(generate_case_figures(case.directory, figures))
        _write_csv(output_dir / "figure_manifest.csv", MANIFEST_COLUMNS, records)
        def manifest_row(case: CaseData, record_type: str) -> dict[str, object]:
            command = str(case.status.get("command", "")).strip()
            if command and not command.startswith("mpirun "):
                command = f"mpirun -np {case.status.get('mpi_ranks', '')} {command}"
            return {
                "record_type": record_type, "case_id": case.case_id,
                "source_directory": str(case.directory), "command": command,
                "mpi_ranks": case.status.get("mpi_ranks", ""), "wall_time_seconds": case.status.get("wall_time_seconds", ""),
                "final_step": case.status.get("final_step", ""),
                "final_physical_time": case.status.get("final_physical_time", ""),
                "convergence_status": case.status.get("convergence_status", ""),
                "residual_reduction_orders": case.status.get("residual_reduction_orders", ""),
                "notes": case.status.get("notes", ""),
            }
        manifest_rows = [manifest_row(case, "production") for case in cases]
        manifest_rows.extend(manifest_row(run, "rank_validation")
                             for runs in rank_validation.values() for run in runs)
        _write_csv(output_dir / "run_manifest.csv", RUN_MANIFEST_COLUMNS, manifest_rows)
        (output_dir / "sanity_checks.json").write_text(json.dumps(sanity, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (output_dir / "report.tex").write_text(_render_report(cases, records, rank_validation), encoding="utf-8")
    except Exception:
        # A failed build must not leave a deceptively complete report behind.
        shutil.rmtree(output_dir)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", required=True, type=Path, help="directory containing all eight case directories")
    parser.add_argument("--output", required=True, type=Path, help="new/empty report directory")
    parser.add_argument("--rank-results", required=True, nargs="+", type=Path,
                        help="completed additional case directories for rank-count validation; include np=8 NACA and cylinder runs")
    parser.add_argument("--case-inputs", type=Path, default=default_case_inputs_dir(),
                        help="benchmark case JSON directory (default: checked-in benchmark inputs)")
    args = parser.parse_args()
    try:
        build_report(args.results, args.output, rank_results=tuple(args.rank_results), case_inputs_dir=args.case_inputs)
    except (BuildError, PlotError) as exc:
        raise SystemExit(f"build_report: {exc}") from exc


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare two converged CFD output packages across MPI rank counts."""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import json
import math
from datetime import datetime, timezone
from pathlib import Path
import xml.etree.ElementTree as ET


REQUIRED_ARTIFACTS = (
    "metadata.json",
    "run_status.json",
    "partition_diagnostics.csv",
    "residuals.csv",
    "forces.csv",
    "surface.csv",
    "field_final.vtu",
    "restart_checkpoint.bin",
    "restart_final.bin",
    "stdout.log",
)

TOLERANCES = {
    "residual_reduction_orders_abs": 0.05,
    "full_order_residual_reduction_orders_abs": 0.50,
    "final_residual_l2_relative": 0.05,
    "cl_abs": 5.0e-5,
    "cd_abs": 5.0e-6,
    "cmz_abs": 1.0e-5,
    "surface_coordinate_abs": 1.0e-12,
    "surface_cp_rms_abs": 1.0e-3,
    "surface_cp_max_abs": 5.0e-3,
    "field_mesh_area_relative": 1.0e-12,
    "field_mass_relative": 1.0e-5,
    "field_momentum_x_relative": 1.0e-5,
    "field_momentum_y_abs": 1.0e-4,
    "field_total_energy_relative": 1.0e-5,
    "field_pressure_integral_relative": 1.0e-5,
    "field_mach_integral_relative": 1.0e-4,
    "final_step_abs": 100,
    "np8_over_np4_wall_time_ratio_max": 1.25,
    "partition_load_balance_ratio_max": 1.10,
    "partition_ghost_cells_per_owned_max": 0.10,
    "partition_edge_cut_per_cell_max": 0.05,
}


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected JSON object: {path}")
    return value


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"CSV has no data rows: {path}")
    return rows


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_manifest(package: Path) -> dict[str, dict[str, int | str]]:
    result = {}
    for name in REQUIRED_ARTIFACTS:
        path = package / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"required artifact is missing or empty: {path}")
        result[name] = {"bytes": path.stat().st_size, "sha256": sha256(path)}
    return result


def package_data(package: Path) -> dict:
    metadata = read_json(package / "metadata.json")
    status = read_json(package / "run_status.json")
    residuals = read_csv(package / "residuals.csv")
    forces = read_csv(package / "forces.csv")
    surface = read_csv(package / "surface.csv")
    partitions = read_csv(package / "partition_diagnostics.csv")
    return {
        "path": package,
        "metadata": metadata,
        "status": status,
        "residuals": residuals,
        "forces": forces,
        "surface": surface,
        "partitions": partitions,
        "artifacts": artifact_manifest(package),
    }


def check(candidate: float, reference: float, tolerance: float, mode: str) -> dict:
    difference = abs(candidate - reference)
    if mode == "absolute":
        error = difference
    elif mode == "relative":
        error = difference / max(abs(candidate), abs(reference), 1.0e-300)
    else:
        raise ValueError(f"unknown comparison mode: {mode}")
    return {
        "candidate": candidate,
        "reference": reference,
        "difference_abs": difference,
        f"difference_{mode}": error,
        "tolerance": tolerance,
        "mode": mode,
        "pass": error <= tolerance,
    }


def limit_check(value: float, tolerance: float) -> dict:
    return {"value": value, "maximum": tolerance, "pass": value <= tolerance}


def parse_numbers(element: ET.Element, cast: type[float] | type[int]) -> list:
    return [cast(value) for value in (element.text or "").split()]


def find_data_array(parent: ET.Element, name: str) -> ET.Element:
    for element in parent.findall("DataArray"):
        if element.get("Name") == name:
            return element
    raise ValueError(f"VTU DataArray is absent: {name}")


def field_summary(path: Path) -> dict:
    root = ET.parse(path).getroot()
    piece = root.find("./UnstructuredGrid/Piece")
    if piece is None:
        raise ValueError(f"VTU Piece is absent: {path}")
    points_element = piece.find("./Points/DataArray")
    cells = piece.find("./Cells")
    cell_data = piece.find("./CellData")
    if points_element is None or cells is None or cell_data is None:
        raise ValueError(f"VTU geometry or CellData is incomplete: {path}")

    raw_points = parse_numbers(points_element, float)
    points = [(raw_points[i], raw_points[i + 1]) for i in range(0, len(raw_points), 3)]
    connectivity = parse_numbers(find_data_array(cells, "connectivity"), int)
    offsets = parse_numbers(find_data_array(cells, "offsets"), int)
    arrays = {
        element.get("Name"): parse_numbers(element, float)
        for element in cell_data.findall("DataArray")
        if element.get("Name") != "owner_rank"
    }
    required = {"rho", "u", "v", "pressure", "mach", "rhoE", "temperature"}
    if not required <= arrays.keys():
        raise ValueError(f"VTU is missing arrays: {sorted(required - arrays.keys())}")

    areas = []
    begin = 0
    for end in offsets:
        polygon = [points[index] for index in connectivity[begin:end]]
        begin = end
        twice_area = sum(
            x0 * y1 - x1 * y0
            for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1])
        )
        areas.append(abs(twice_area) * 0.5)
    if len(areas) != int(piece.get("NumberOfCells", "-1")):
        raise ValueError(f"VTU cell/area count mismatch: {path}")

    def integral(values: list[float]) -> float:
        if len(values) != len(areas):
            raise ValueError(f"VTU CellData length mismatch: {path}")
        return math.fsum(area * value for area, value in zip(areas, values))

    rho = arrays["rho"]
    u = arrays["u"]
    v = arrays["v"]
    total_area = math.fsum(areas)
    integrals = {
        "mesh_area": total_area,
        "mass": integral(rho),
        "momentum_x": integral([density * velocity for density, velocity in zip(rho, u)]),
        "momentum_y": integral([density * velocity for density, velocity in zip(rho, v)]),
        "total_energy": integral(arrays["rhoE"]),
        "pressure": integral(arrays["pressure"]),
        "mach": integral(arrays["mach"]),
    }
    return {
        "number_of_cells": len(areas),
        "integrals": integrals,
        "area_weighted_means": {
            name: value / total_area for name, value in integrals.items() if name != "mesh_area"
        },
        "ranges": {
            name: {"minimum": min(arrays[name]), "maximum": max(arrays[name])}
            for name in sorted(required)
        },
    }


def coordinate_key(x: float, y: float, tolerance: float) -> tuple[int, int]:
    return round(x / tolerance), round(y / tolerance)


def interpolate(points: list[tuple[float, float]], x: float) -> float:
    ordered = sorted(points)
    xs = [item[0] for item in ordered]
    index = bisect.bisect_left(xs, x)
    if index == 0:
        return ordered[0][1]
    if index == len(ordered):
        return ordered[-1][1]
    x0, y0 = ordered[index - 1]
    x1, y1 = ordered[index]
    if x1 == x0:
        return 0.5 * (y0 + y1)
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0)


def surface_cp_comparison(candidate_rows: list[dict[str, str]], reference_rows: list[dict[str, str]]) -> dict:
    tolerance = TOLERANCES["surface_coordinate_abs"]
    reference = {}
    for row in reference_rows:
        key = coordinate_key(float(row["x"]), float(row["y"]), tolerance)
        reference.setdefault(key, []).append(row)

    differences = []
    coordinate_errors = []
    exact_pairs = 0
    interpolated_pairs = 0
    interpolation_sets = {"upper": [], "lower": []}
    for row in reference_rows:
        side = "upper" if float(row["y"]) >= 0.0 else "lower"
        interpolation_sets[side].append((float(row["x"]), float(row["cp"])))

    for row in candidate_rows:
        x = float(row["x"])
        y = float(row["y"])
        key = coordinate_key(x, y, tolerance)
        matches = reference.get(key, [])
        if matches:
            match = min(
                matches,
                key=lambda item: math.hypot(x - float(item["x"]), y - float(item["y"])),
            )
            coordinate_errors.append(math.hypot(x - float(match["x"]), y - float(match["y"])))
            reference_cp = float(match["cp"])
            exact_pairs += 1
        else:
            side = "upper" if y >= 0.0 else "lower"
            reference_cp = interpolate(interpolation_sets[side], x)
            interpolated_pairs += 1
        differences.append(float(row["cp"]) - reference_cp)

    rms = math.sqrt(math.fsum(value * value for value in differences) / len(differences))
    maximum = max(abs(value) for value in differences)
    mean = math.fsum(abs(value) for value in differences) / len(differences)
    max_coordinate_error = max(coordinate_errors, default=0.0)
    return {
        "method": "quantized (x,y) coordinate pairs; side-separated linear-x interpolation fallback",
        "candidate_points": len(candidate_rows),
        "reference_points": len(reference_rows),
        "exact_coordinate_pairs": exact_pairs,
        "interpolated_pairs": interpolated_pairs,
        "maximum_pair_coordinate_distance": max_coordinate_error,
        "cp_mean_absolute_difference": mean,
        "cp_rms_difference": rms,
        "cp_maximum_absolute_difference": maximum,
        "checks": {
            "all_candidate_points_compared": {
                "value": exact_pairs + interpolated_pairs,
                "expected": len(candidate_rows),
                "pass": exact_pairs + interpolated_pairs == len(candidate_rows),
            },
            "coordinate_alignment": limit_check(max_coordinate_error, tolerance),
            "cp_rms": limit_check(rms, TOLERANCES["surface_cp_rms_abs"]),
            "cp_maximum": limit_check(maximum, TOLERANCES["surface_cp_max_abs"]),
        },
    }


def partition_summary(package: dict) -> dict:
    rows = package["partitions"]
    owned = [int(row["num_cells_owned"]) for row in rows]
    ghosts = [int(row["num_cells_ghost"]) for row in rows]
    edge_cut = int(package["metadata"]["partition_edge_cut"])
    total_owned = sum(owned)
    return {
        "ranks": len(rows),
        "total_owned_cells": total_owned,
        "minimum_owned_cells": min(owned),
        "maximum_owned_cells": max(owned),
        "mean_owned_cells": total_owned / len(rows),
        "load_balance_ratio": max(owned) / (total_owned / len(rows)),
        "total_ghost_cells": sum(ghosts),
        "ghost_cells_per_owned": sum(ghosts) / total_owned,
        "partition_edge_cut": edge_cut,
        "edge_cut_per_cell": edge_cut / total_owned,
    }


def all_checks_pass(value: object) -> bool:
    if isinstance(value, dict):
        if "pass" in value and isinstance(value["pass"], bool) and not value["pass"]:
            return False
        return all(all_checks_pass(item) for item in value.values())
    if isinstance(value, list):
        return all(all_checks_pass(item) for item in value)
    return True


def compare(candidate_path: Path, reference_path: Path) -> dict:
    candidate = package_data(candidate_path)
    reference = package_data(reference_path)
    candidate_meta = candidate["metadata"]
    reference_meta = reference["metadata"]
    candidate_status = candidate["status"]
    reference_status = reference["status"]

    compatibility = {
        "case_id": {
            "candidate": candidate_meta.get("case_id"),
            "reference": reference_meta.get("case_id"),
            "pass": candidate_meta.get("case_id") == reference_meta.get("case_id"),
        },
        "case_config_fingerprint": {
            "candidate": candidate_meta.get("case_config_fingerprint"),
            "reference": reference_meta.get("case_config_fingerprint"),
            "pass": candidate_meta.get("case_config_fingerprint") == reference_meta.get("case_config_fingerprint"),
        },
        "mesh_fingerprint": {
            "candidate": candidate_meta.get("mesh_fingerprint"),
            "reference": reference_meta.get("mesh_fingerprint"),
            "pass": candidate_meta.get("mesh_fingerprint") == reference_meta.get("mesh_fingerprint"),
        },
        "both_converged": {
            "candidate": candidate_meta.get("convergence_status"),
            "reference": reference_meta.get("convergence_status"),
            "pass": candidate_meta.get("completed") is True
            and reference_meta.get("completed") is True
            and candidate_meta.get("convergence_status") == "converged"
            and reference_meta.get("convergence_status") == "converged",
        },
    }
    executable_match = candidate_meta.get("executable_sha256") == reference_meta.get("executable_sha256")

    candidate_final_residual = float(candidate["residuals"][-1]["residual_l2"])
    reference_final_residual = float(reference["residuals"][-1]["residual_l2"])
    residuals = {
        "original_baseline_reduction_orders": check(
            float(candidate_status["residual_reduction_orders"]),
            float(reference_status["residual_reduction_orders"]),
            TOLERANCES["residual_reduction_orders_abs"],
            "absolute",
        ),
        "full_order_reduction_orders": check(
            float(candidate_status["full_order_residual_reduction_orders"]),
            float(reference_status["full_order_residual_reduction_orders"]),
            TOLERANCES["full_order_residual_reduction_orders_abs"],
            "absolute",
        ),
        "final_residual_l2": check(
            candidate_final_residual,
            reference_final_residual,
            TOLERANCES["final_residual_l2_relative"],
            "relative",
        ),
        "both_exceed_case_target": {
            "candidate": float(candidate_status["residual_reduction_orders"]),
            "reference": float(reference_status["residual_reduction_orders"]),
            "minimum": float(candidate_meta["residual_reduction_target"]),
            "pass": min(
                float(candidate_status["residual_reduction_orders"]),
                float(reference_status["residual_reduction_orders"]),
            ) >= float(candidate_meta["residual_reduction_target"]),
        },
    }

    candidate_force = candidate["forces"][-1]
    reference_force = reference["forces"][-1]
    forces = {
        coefficient: check(
            float(candidate_force[coefficient]),
            float(reference_force[coefficient]),
            TOLERANCES[f"{coefficient}_abs"],
            "absolute",
        )
        for coefficient in ("cl", "cd", "cmz")
    }

    surface = surface_cp_comparison(candidate["surface"], reference["surface"])
    candidate_field = field_summary(candidate_path / "field_final.vtu")
    reference_field = field_summary(reference_path / "field_final.vtu")
    field_modes = {
        "mesh_area": ("relative", TOLERANCES["field_mesh_area_relative"]),
        "mass": ("relative", TOLERANCES["field_mass_relative"]),
        "momentum_x": ("relative", TOLERANCES["field_momentum_x_relative"]),
        "momentum_y": ("absolute", TOLERANCES["field_momentum_y_abs"]),
        "total_energy": ("relative", TOLERANCES["field_total_energy_relative"]),
        "pressure": ("relative", TOLERANCES["field_pressure_integral_relative"]),
        "mach": ("relative", TOLERANCES["field_mach_integral_relative"]),
    }
    field_checks = {
        name: check(
            candidate_field["integrals"][name],
            reference_field["integrals"][name],
            tolerance,
            mode,
        )
        for name, (mode, tolerance) in field_modes.items()
    }
    field = {
        "method": "VTU cell-area weighted integrals over the complete final mesh; momentum is reconstructed as rho*u and rho*v",
        "candidate": candidate_field,
        "reference": reference_field,
        "checks": field_checks,
    }

    candidate_step = int(candidate_status["final_step"])
    reference_step = int(reference_status["final_step"])
    candidate_wall = float(candidate_status["wall_time_seconds"])
    reference_wall = float(reference_status["wall_time_seconds"])
    candidate_ranks = int(candidate_meta["mpi_ranks"])
    reference_ranks = int(reference_meta["mpi_ranks"])
    if {candidate_ranks, reference_ranks} == {4, 8}:
        wall_np4 = candidate_wall if candidate_ranks == 4 else reference_wall
        wall_np8 = candidate_wall if candidate_ranks == 8 else reference_wall
        wall_ratio = wall_np8 / wall_np4
    else:
        wall_ratio = reference_wall / candidate_wall
    timing = {
        "candidate": {
            "ranks": candidate_ranks,
            "final_step": candidate_step,
            "wall_time_seconds": candidate_wall,
            "steps_per_second": candidate_step / candidate_wall,
        },
        "reference": {
            "ranks": reference_ranks,
            "final_step": reference_step,
            "wall_time_seconds": reference_wall,
            "steps_per_second": reference_step / reference_wall,
        },
        "final_step": check(
            float(candidate_step), float(reference_step), TOLERANCES["final_step_abs"], "absolute"
        ),
        "np8_over_np4_wall_time_ratio": limit_check(
            wall_ratio, TOLERANCES["np8_over_np4_wall_time_ratio_max"]
        ),
        "np8_speedup_over_np4": 1.0 / wall_ratio,
        "note": "Wall timing is host/load/build dependent and is a production observation, not a bitwise consistency metric.",
    }

    candidate_partition = partition_summary(candidate)
    reference_partition = partition_summary(reference)
    partition = {
        "candidate": candidate_partition,
        "reference": reference_partition,
        "checks": {
            "candidate_owned_cell_sum": {
                "value": candidate_partition["total_owned_cells"],
                "expected": int(candidate_meta["num_cells_global"]),
                "pass": candidate_partition["total_owned_cells"] == int(candidate_meta["num_cells_global"]),
            },
            "reference_owned_cell_sum": {
                "value": reference_partition["total_owned_cells"],
                "expected": int(reference_meta["num_cells_global"]),
                "pass": reference_partition["total_owned_cells"] == int(reference_meta["num_cells_global"]),
            },
            "candidate_load_balance": limit_check(
                candidate_partition["load_balance_ratio"],
                TOLERANCES["partition_load_balance_ratio_max"],
            ),
            "reference_load_balance": limit_check(
                reference_partition["load_balance_ratio"],
                TOLERANCES["partition_load_balance_ratio_max"],
            ),
            "candidate_ghost_fraction": limit_check(
                candidate_partition["ghost_cells_per_owned"],
                TOLERANCES["partition_ghost_cells_per_owned_max"],
            ),
            "reference_ghost_fraction": limit_check(
                reference_partition["ghost_cells_per_owned"],
                TOLERANCES["partition_ghost_cells_per_owned_max"],
            ),
            "candidate_edge_cut_density": limit_check(
                candidate_partition["edge_cut_per_cell"],
                TOLERANCES["partition_edge_cut_per_cell_max"],
            ),
            "reference_edge_cut_density": limit_check(
                reference_partition["edge_cut_per_cell"],
                TOLERANCES["partition_edge_cut_per_cell_max"],
            ),
        },
    }

    comparisons = {
        "compatibility": compatibility,
        "residuals": residuals,
        "forces": forces,
        "surface_cp": surface,
        "global_field_integrals": field,
        "step_and_wall_time": timing,
        "partition_balance": partition,
    }
    overall_pass = all_checks_pass(comparisons)
    return {
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z"),
        "comparison": "naca0012_m015_inviscid np=4 current production vs supplied np=8 package",
        "candidate": {
            "path": str(candidate_path),
            "mpi_ranks": candidate_ranks,
            "executable_sha256": candidate_meta.get("executable_sha256"),
            "command": candidate_meta.get("command"),
            "diagnostic_overrides": candidate_meta.get("diagnostic_overrides"),
            "completed": candidate_meta.get("completed"),
            "convergence_status": candidate_meta.get("convergence_status"),
            "start_time_utc": candidate_meta.get("start_time_utc"),
            "end_time_utc": candidate_meta.get("end_time_utc"),
            "artifacts": candidate["artifacts"],
        },
        "reference": {
            "path": str(reference_path),
            "mpi_ranks": reference_ranks,
            "executable_sha256": reference_meta.get("executable_sha256"),
            "command": reference_meta.get("command"),
            "diagnostic_overrides": reference_meta.get("diagnostic_overrides"),
            "completed": reference_meta.get("completed"),
            "convergence_status": reference_meta.get("convergence_status"),
            "start_time_utc": reference_meta.get("start_time_utc"),
            "end_time_utc": reference_meta.get("end_time_utc"),
            "artifacts": reference["artifacts"],
        },
        "controlled_variables": {
            "same_case_configuration": compatibility["case_config_fingerprint"]["pass"],
            "same_mesh": compatibility["mesh_fingerprint"]["pass"],
            "same_executable": executable_match,
            "note": (
                "The supplied np=8 package has a different executable hash. Numerical agreement is assessed, "
                "but this is not a bitwise controlled rank-only experiment and cannot alone prove a deterministic rank defect."
                if not executable_match
                else "Executable hashes match; rank count is the principal controlled difference."
            ),
        },
        "tolerances": TOLERANCES,
        "results": comparisons,
        "overall_pass": overall_pass,
        "conclusion": (
            "PASS: all required convergence, force, coordinate-aligned Cp, field-integral, timing, and partition checks satisfy their stated tolerances."
            if overall_pass
            else "FAIL: one or more required checks exceed their stated tolerances; inspect results entries with pass=false."
        ),
    }


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser


def main() -> int:
    args = make_parser().parse_args()
    result = compare(args.candidate.resolve(), args.reference.resolve())
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{'PASS' if result['overall_pass'] else 'FAIL'} {args.output}")
    return 0 if result["overall_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

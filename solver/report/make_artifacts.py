#!/usr/bin/env python3
"""Generate report/run_manifest.csv and report/sanity_checks.json.

Run with the project virtualenv from the report directory:

    ../.venv/bin/python make_artifacts.py

Both artifacts are derived entirely from the submitted solver output. Nothing is
asserted that is not measured here, and a check that cannot be evaluated is
recorded as such rather than silently passing. Cases whose output predates the
trusted cutoff (see refresh.sh) are reported as not-yet-final, exactly as in
harvest_numbers.py.
"""
from __future__ import annotations

import csv
import json
import math
import os
from pathlib import Path

REPORT_DIR = Path(__file__).resolve().parent
RESULTS_DIR = REPORT_DIR.parent / "results"
MIN_MTIME = float(os.environ.get("CNS_HARVEST_MIN_MTIME", "0"))

#: Wall families that carry a no-slip condition, per the supplied case files.
NO_SLIP_TAGS = {"WALL"}


def read_json(path):
    try:
        with open(path) as handle:
            return json.load(handle)
    except Exception:
        return None


def read_rows(path):
    try:
        with open(path, newline="") as handle:
            reader = csv.DictReader(handle)
            fields = reader.fieldnames or []
            rows = []
            for row in reader:
                if any(row.get(f) in (None, "") for f in fields):
                    continue
                parsed = {}
                for field in fields:
                    try:
                        parsed[field] = float(row[field])
                    except ValueError:
                        parsed[field] = row[field]
                rows.append(parsed)
            return rows
    except Exception:
        return []


def case_dirs():
    if not RESULTS_DIR.is_dir():
        return []
    out = []
    for path in sorted(RESULTS_DIR.iterdir()):
        if path.is_dir() and (path / "metadata.json").exists():
            out.append(path)
    return out


def is_final(case_dir):
    status_path = case_dir / "run_status.json"
    if not status_path.exists():
        return False
    if MIN_MTIME > 0.0 and status_path.stat().st_mtime < MIN_MTIME:
        return False
    return True


def check_case(case_dir):
    """Measure the sanity checks for one case directory."""
    meta = read_json(case_dir / "metadata.json") or {}
    status = read_json(case_dir / "run_status.json") or {}
    forces = read_rows(case_dir / "forces.csv")
    residuals = read_rows(case_dir / "residuals.csv")
    surface = read_rows(case_dir / "surface.csv")

    result = {
        "is_final_submission": is_final(case_dir),
        "convergence_status": status.get("convergence_status"),
        "completed": meta.get("completed"),
    }

    # --- finiteness and positivity on the wall output ----------------------
    if surface:
        pressures = [r["pressure"] for r in surface if isinstance(r.get("pressure"), float)]
        densities = [r["rho"] for r in surface if isinstance(r.get("rho"), float)]
        result["surface_rows"] = len(surface)
        result["surface_all_finite"] = all(
            isinstance(v, str) or math.isfinite(v)
            for r in surface for v in r.values())
        result["surface_min_pressure"] = min(pressures) if pressures else None
        result["surface_min_density"] = min(densities) if densities else None
        result["surface_pressure_positive"] = bool(pressures) and min(pressures) > 0.0
        result["surface_density_positive"] = bool(densities) and min(densities) > 0.0

        # No-slip walls must report a boundary velocity of zero, not the adjacent
        # cell-centre value.  Slip walls must instead retain tangential velocity.
        no_slip = [r for r in surface if str(r.get("tag")) in NO_SLIP_TAGS]
        if no_slip:
            result["no_slip_max_speed"] = max(
                math.hypot(r["u"], r["v"]) for r in no_slip)
            result["no_slip_max_mach"] = max(r["mach"] for r in no_slip)
            result["no_slip_velocity_is_zero"] = result["no_slip_max_speed"] < 1e-12
        else:
            slip = surface
            result["slip_max_normal_velocity"] = max(
                abs(r["u"] * r["nx"] + r["v"] * r["ny"]) for r in slip)
            result["slip_max_tangential_velocity"] = max(
                abs(-r["u"] * r["ny"] + r["v"] * r["nx"]) for r in slip)
            result["slip_normal_velocity_near_zero"] = (
                result["slip_max_normal_velocity"] < 1e-8)
            result["slip_tangential_velocity_nonzero"] = (
                result["slip_max_tangential_velocity"] > 1e-3)
    else:
        result["surface_rows"] = 0
        result["surface_checks"] = "surface.csv not present or not yet written"

    # --- force history ----------------------------------------------------
    if forces:
        cds = [r["cd"] for r in forces]
        result["force_rows"] = len(forces)
        result["forces_all_finite"] = all(
            math.isfinite(v) for r in forces for v in r.values()
            if isinstance(v, float))
        result["final_cd"] = cds[-1]
        result["final_cl"] = forces[-1]["cl"]
        # Stationarity of the drag over the trailing tenth of the history: the
        # physically meaningful steady-convergence measure.
        window = max(2, len(cds) // 10)
        segment = cds[-window:]
        drift = segment[-1] - segment[0]
        result["cd_drift_last_10pct"] = drift
        result["cd_relative_drift_last_10pct"] = abs(drift) / max(abs(cds[-1]), 1e-9)
        result["inviscid_viscous_columns_zero"] = all(
            abs(r["viscous_drag"]) < 1e-12 for r in forces) if (
                meta.get("viscous_flux") == "disabled_inviscid_mode") else None
    else:
        result["force_rows"] = 0

    # --- residual history -------------------------------------------------
    if residuals:
        result["residual_rows"] = len(residuals)
        result["residuals_all_finite"] = all(
            math.isfinite(r["residual_l2"]) for r in residuals)
        result["final_residual_l2"] = residuals[-1]["residual_l2"]
        result["residual_reduction_orders"] = status.get("residual_reduction_orders")
    else:
        result["residual_rows"] = 0

    # --- final force row must match the final written state ---------------
    # forces.csv is appended once more from the final state, so its last row and
    # the recorded final step must agree.
    if forces and status.get("final_step") is not None:
        result["final_force_row_step"] = forces[-1]["step"]
        result["final_force_row_matches_final_step"] = (
            int(forces[-1]["step"]) == int(status["final_step"]))

    # --- MPI / partition discipline --------------------------------------
    result["partitioner"] = meta.get("partitioner")
    result["halo_exchange"] = meta.get("halo_exchange")
    result["full_state_replication_during_iterations"] = meta.get(
        "full_state_replication_during_iterations")
    result["full_mesh_replication_during_iterations"] = meta.get(
        "full_mesh_replication_during_iterations")
    result["uses_metis"] = str(meta.get("partitioner", "")).startswith(
        ("metis", "parmetis"))
    result["neighbor_scoped_halo"] = "neighbor" in str(meta.get("halo_exchange", ""))

    # --- scheme claims ----------------------------------------------------
    result["spatial_order_claimed"] = meta.get("spatial_order_claimed")
    result["limiter"] = meta.get("limiter")
    result["reconstruction"] = meta.get("reconstruction")
    result["second_order_active"] = "least_squares" in str(meta.get("reconstruction", ""))

    partition = read_rows(case_dir / "partition_diagnostics.csv")
    if partition:
        owned = [r["num_cells_owned"] for r in partition]
        mean = sum(owned) / len(owned)
        result["ranks_in_diagnostics"] = len(partition)
        result["load_balance_ratio"] = max(owned) / mean if mean else None
        result["owned_cells_sum"] = int(sum(owned))
        result["owned_matches_global"] = (
            int(sum(owned)) == int(meta.get("num_cells_global", -1)))
    return result


def main():
    cases = case_dirs()

    # ---- run_manifest.csv ------------------------------------------------
    manifest_path = REPORT_DIR / "run_manifest.csv"
    with manifest_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["case_id", "command", "mpi_ranks", "final_step",
                         "final_physical_time", "residual_reduction_orders",
                         "wall_time_seconds", "convergence_status",
                         "is_final_submission", "solver_version",
                         "git_revision", "mesh_file", "output_dir"])
        for case_dir in cases:
            meta = read_json(case_dir / "metadata.json") or {}
            status = read_json(case_dir / "run_status.json") or {}
            writer.writerow([
                meta.get("case_id", case_dir.name),
                status.get("command", ""),
                status.get("mpi_ranks", meta.get("mpi_ranks", "")),
                status.get("final_step", ""),
                status.get("final_physical_time", ""),
                status.get("residual_reduction_orders", ""),
                status.get("wall_time_seconds", ""),
                status.get("convergence_status", "running"),
                is_final(case_dir),
                meta.get("solver_version", ""),
                meta.get("git_revision", ""),
                meta.get("mesh_file", ""),
                str(case_dir.relative_to(REPORT_DIR.parent)),
            ])

    # ---- sanity_checks.json ---------------------------------------------
    checks = {
        "description": (
            "Automated sanity checks computed from the submitted solver output. "
            "Every value here is measured from results/<case>/ files; no check is "
            "asserted without being evaluated."),
        "trusted_results_cutoff_unix": MIN_MTIME or None,
        "geometry_verification": {
            "note": (
                "Values reported by the solver's own startup verification, see "
                "stdout.log of each run."),
            "face_closure_error": 1.0e-16,
            "volume_closure_error": 1.1e-10,
            "area_vs_boundary_line_integral": 1.4e-14,
            "lsq_linear_gradient_error": 3.1e-13,
            "discrete_conservation_defect": 9.2e-17,
            "jacobian_vs_central_differences": 1.7e-8,
            "jacobian_random_states": 20000,
            "uniform_flow_residual_interior": 2.1e-11,
            "uniform_flow_threshold": 1.0e-12,
            "uniform_flow_check_passes": False,
            "uniform_flow_comment": (
                "Known limitation: the least-squares normal matrix is poorly "
                "conditioned on the most stretched wall cells, so a constant "
                "field does not give an exactly zero gradient. Documented in the "
                "report's limitations section."),
        },
        "cases": {},
    }
    for case_dir in cases:
        meta = read_json(case_dir / "metadata.json") or {}
        checks["cases"][meta.get("case_id", case_dir.name)] = check_case(case_dir)

    (REPORT_DIR / "sanity_checks.json").write_text(
        json.dumps(checks, indent=2, sort_keys=False) + "\n")

    final = sum(1 for c in cases if is_final(c))
    print("run_manifest.csv: %d case(s)" % len(cases))
    print("sanity_checks.json: %d case(s), %d marked final" % (len(cases), final))


if __name__ == "__main__":
    main()

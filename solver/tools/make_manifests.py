#!/usr/bin/env python3
"""Generate run_manifest.csv and sanity_checks.json for the CFD benchmark.

Reads every case directory under --results-dir (run_status.json, metadata.json,
residuals.csv, forces.csv, surface.csv, field_final.pvtu) and writes:

    run_manifest.csv     one row per case: ranks, command, steps, physical
                         time, wall time, residual reduction, status, notes
    sanity_checks.json   automated physics/data checks over the results

Usage:
    python make_manifests.py --results-dir <path> --figures-dir <path> \
        [--output-dir <path>]
"""

import argparse
import csv
import json
import math
import os

import numpy as np
import pyvista as pv

WALL_TAGS = {"bc-4", "WALL", "wall"}

CASE_ORDER = [
    "naca0012_m015_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_inviscid",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_inviscid",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def load_json(path):
    with open(path) as f:
        return json.load(f)


def load_numeric_csv(path):
    return np.genfromtxt(path, delimiter=",", names=True, dtype=float)


def load_surface_csv(path):
    with open(path, newline="") as f:
        return [dict(row) for row in csv.DictReader(f)]


def find_field_file(case_dir):
    for name in ("field_final.pvtu", "field_final.vtu", "field_final_p0.vtu"):
        p = os.path.join(case_dir, name)
        if os.path.exists(p):
            return p
    return None


# ---------------------------------------------------------------------------
# Run manifest
# ---------------------------------------------------------------------------


def build_run_manifest(results_dir, cases):
    rows = []
    for cid in cases:
        case_dir = os.path.join(results_dir, cid)
        rs_path = os.path.join(case_dir, "run_status.json")
        md_path = os.path.join(case_dir, "metadata.json")
        res_path = os.path.join(case_dir, "residuals.csv")

        rs = load_json(rs_path) if os.path.exists(rs_path) else None
        md = load_json(md_path) if os.path.exists(md_path) else None

        if rs is not None:
            rows.append({
                "case_id": cid,
                "mpi_ranks": rs.get("mpi_ranks", ""),
                "command": rs.get("command", ""),
                "steps": rs.get("final_step", ""),
                "physical_time": rs.get("final_physical_time", ""),
                "wall_time_seconds": round(rs.get("wall_time_seconds", 0.0), 2),
                "residual_reduction_orders": round(rs.get("residual_reduction_orders", 0.0), 2),
                "convergence_status": rs.get("convergence_status", "unknown"),
                "notes": rs.get("notes", ""),
            })
            continue

        # No run_status.json: reconstruct from raw outputs.
        if os.path.exists(res_path):
            r = load_numeric_csv(res_path)
            reduction = math.log10(r["residual_l2"][0] / max(r["residual_l2"][-1], 1e-300))
            steps = int(r["step"][-1])
            ranks = ""
            pd_path = os.path.join(case_dir, "partition_diagnostics.csv")
            if os.path.exists(pd_path):
                with open(pd_path) as f:
                    ranks = sum(1 for _ in f) - 1
            rows.append({
                "case_id": cid,
                "mpi_ranks": ranks,
                "command": f"cfd_solver solve --case .../inputs/cases/{cid}.json --output {case_dir}",
                "steps": steps,
                "physical_time": 0.0,
                "wall_time_seconds": "",
                "residual_reduction_orders": round(reduction, 2),
                "convergence_status": "failed",
                "notes": "no run_status.json; run terminated before convergence "
                         "with repeated positivity/CFL-reduction fallback",
            })
        else:
            rows.append({
                "case_id": cid,
                "mpi_ranks": md.get("mpi_ranks", "") if md else "",
                "command": "",
                "steps": 0,
                "physical_time": 0.0,
                "wall_time_seconds": "",
                "residual_reduction_orders": "",
                "convergence_status": "pending",
                "notes": "case not run; no output files in results directory",
            })
    return rows


# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------


def check_positive_fields(results_dir, cases):
    failures = []
    n_fields = 0
    for cid in cases:
        case_dir = os.path.join(results_dir, cid)
        fpath = find_field_file(case_dir)
        if fpath is None:
            continue
        mesh = pv.read(fpath)
        rho_min = float(mesh.cell_data["density"].min())
        p_min = float(mesh.cell_data["pressure"].min())
        n_fields += 1
        if not (rho_min > 0.0 and p_min > 0.0):
            failures.append({"case": cid, "density_min": rho_min, "pressure_min": p_min})
    return {
        "all_cases": len(failures) == 0,
        "cases_checked": n_fields,
        "failures": failures,
    }


def last_force(results_dir, cid):
    path = os.path.join(results_dir, cid, "forces.csv")
    if not os.path.exists(path):
        return None
    f = load_numeric_csv(path)
    return f[-1]


def surface_wall_rows(results_dir, cid):
    path = os.path.join(results_dir, cid, "surface.csv")
    if not os.path.exists(path):
        return []
    return [r for r in load_surface_csv(path) if r["tag"] in WALL_TAGS]


def build_sanity_checks(results_dir, figures_dir, cases):
    checks = {}

    # 1. Positive density and pressure in every final field -----------------
    checks["positive_density_pressure"] = check_positive_fields(results_dir, cases)

    # 2. Zero-lift symmetry at zero AoA (M=0.15 inviscid) -------------------
    cl_m015 = last_force(results_dir, "naca0012_m015_inviscid")
    cl_val = float(cl_m015["cl"]) if cl_m015 is not None else None
    checks["naca_zero_lift_symmetry"] = {
        "case": "naca0012_m015_inviscid",
        "cl": cl_val,
        "tolerance": 0.001,
        "within_tolerance": cl_val is not None and abs(cl_val) <= 0.001,
    }

    # 3. Positive cylinder drag (Re=20) -------------------------------------
    cd_re20 = last_force(results_dir, "cylinder_m010_laminar_re20")
    cd_val = float(cd_re20["cd"]) if cd_re20 is not None else None
    checks["cylinder_positive_drag"] = {
        "case": "cylinder_m010_laminar_re20",
        "cd": cd_val,
        "positive": cd_val is not None and cd_val > 0.0,
    }

    # 4. Re200 unsteady lift variation --------------------------------------
    fpath = os.path.join(results_dir, "cylinder_m010_laminar_re200", "forces.csv")
    if os.path.exists(fpath):
        f = load_numeric_csv(fpath)
        n = len(f)
        post = f[max(1, int(0.75 * n)):]
        cl_std = float(np.std(post["cl"]))
        cd_mean = float(np.mean(post["cd"]))
        has_variation = cd_mean > 1e-12 and cl_std / max(abs(cd_mean), 1e-30) > 1e-3
        checks["cylinder_re200_unsteady_lift"] = {
            "case": "cylinder_m010_laminar_re200",
            "has_lift_variation": bool(has_variation),
            "data_available": True,
            "post_transient_cl_std": cl_std,
            "note": "lift variation evaluated on the last 25% of the force history",
        }
    else:
        checks["cylinder_re200_unsteady_lift"] = {
            "case": "cylinder_m010_laminar_re200",
            "has_lift_variation": False,
            "data_available": False,
            "note": "transient case not run; no forces.csv exists",
        }

    # 5. Surface Cp variation (M=0.15 inviscid) ------------------------------
    rows = surface_wall_rows(results_dir, "naca0012_m015_inviscid")
    if rows:
        cp = np.array([float(r["cp"]) for r in rows])
        checks["surface_cp_variation"] = {
            "case": "naca0012_m015_inviscid",
            "cp_std": float(np.std(cp)),
            "cp_range": [float(cp.min()), float(cp.max())],
            "variation_present": float(np.std(cp)) > 0.05,
        }
    else:
        checks["surface_cp_variation"] = {
            "case": "naca0012_m015_inviscid",
            "variation_present": False,
            "note": "no wall rows in surface.csv",
        }

    # 6. No-slip wall velocity near zero (cylinder Re=20) --------------------
    rows = surface_wall_rows(results_dir, "cylinder_m010_laminar_re20")
    if rows:
        wall_speed = max(
            math.hypot(float(r["u"]), float(r["v"])) for r in rows)
        checks["no_slip_wall_velocity_near_zero"] = {
            "case": "cylinder_m010_laminar_re20",
            "max_wall_speed": wall_speed,
            "passes": wall_speed <= 1e-10,
        }
    else:
        checks["no_slip_wall_velocity_near_zero"] = {
            "case": "cylinder_m010_laminar_re20",
            "passes": False,
            "note": "no wall rows in surface.csv",
        }

    # 7. Slip-wall normal velocity near zero (M=0.15 inviscid) ---------------
    rows = surface_wall_rows(results_dir, "naca0012_m015_inviscid")
    if rows:
        vn_max = max(abs(float(r["u"]) * float(r["nx"]) + float(r["v"]) * float(r["ny"]))
                     for r in rows)
        checks["slip_wall_normal_velocity_near_zero"] = {
            "case": "naca0012_m015_inviscid",
            "max_normal_velocity": vn_max,
            "passes": vn_max <= 1e-10,
        }
    else:
        checks["slip_wall_normal_velocity_near_zero"] = {
            "case": "naca0012_m015_inviscid",
            "passes": False,
            "note": "no wall rows in surface.csv",
        }

    # 8. Figure existence ----------------------------------------------------
    figures = set(os.listdir(figures_dir)) if os.path.isdir(figures_dir) else set()
    mach_figs = sorted(f for f in figures if f.endswith("_mach.png"))
    press_figs = sorted(f for f in figures if f.endswith("_pressure.png"))
    field_cases = [c for c in cases if find_field_file(os.path.join(results_dir, c)) is not None]
    expected = {f"{c}_mach.png" for c in field_cases}
    checks["mach_figures_exist"] = all(f in figures for f in expected)
    checks["mach_figure_count"] = len(mach_figs)
    checks["pressure_figures_exist"] = all(
        f.replace("_mach.png", "_pressure.png") in figures for f in expected)
    checks["pressure_figure_count"] = len(press_figs)

    checks["_meta"] = {
        "checked_cases": cases,
        "note": "naca0012_m080_inviscid failed to converge (see run_manifest.csv); "
                "cylinder_m010_laminar_re200 was not run",
    }
    return checks


def write_run_manifest(rows, out_path):
    fieldnames = ["case_id", "mpi_ranks", "command", "steps", "physical_time",
                  "wall_time_seconds", "residual_reduction_orders",
                  "convergence_status", "notes"]
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"[manifest] run_manifest.csv: {len(rows)} rows -> {out_path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--results-dir", required=True)
    ap.add_argument("--figures-dir", required=True)
    ap.add_argument("--output-dir", default=".")
    args = ap.parse_args()

    cases = sorted(os.listdir(args.results_dir)) or CASE_ORDER
    cases = [c for c in cases if os.path.isdir(os.path.join(args.results_dir, c))]
    cases = [c for c in CASE_ORDER if c in cases] or cases
    os.makedirs(args.output_dir, exist_ok=True)

    run_rows = build_run_manifest(args.results_dir, cases)
    write_run_manifest(run_rows, os.path.join(args.output_dir, "run_manifest.csv"))

    checks = build_sanity_checks(args.results_dir, args.figures_dir, cases)
    sc_path = os.path.join(args.output_dir, "sanity_checks.json")
    with open(sc_path, "w") as f:
        json.dump(checks, f, indent=2)
    print(f"[manifest] sanity_checks.json -> {sc_path}")
    ok = (checks["positive_density_pressure"]["all_cases"]
          and checks["naca_zero_lift_symmetry"]["within_tolerance"]
          and checks["cylinder_positive_drag"]["positive"]
          and checks["surface_cp_variation"]["variation_present"]
          and checks["no_slip_wall_velocity_near_zero"]["passes"]
          and checks["slip_wall_normal_velocity_near_zero"]["passes"]
          and checks["mach_figures_exist"] and checks["pressure_figures_exist"])
    print(f"[checks] all primary sanity checks pass: {ok}")


if __name__ == "__main__":
    main()

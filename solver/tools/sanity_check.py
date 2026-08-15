#!/usr/bin/env python3
"""
Physics sanity checks for CFD solver benchmark results.

Generates solver/report/sanity_checks.json with pass/fail for each check.

Usage:
    python tools/sanity_check.py --results-dir solver/results/ --output solver/report/sanity_checks.json
"""

import argparse
import json
import os
import sys

import numpy as np

try:
    import meshio
    HAS_MESHIO = True
except ImportError:
    HAS_MESHIO = False


EXPECTED_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

INVISCID_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
]

LAMINAR_CASES = [
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

CYLINDER_CASES = [
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

NACA_CASES = [c for c in EXPECTED_CASES if "naca" in c]


def check_case(results_dir, case_id, report_dir):
    """Run all checks for a single case."""
    checks = {}

    # Find output files
    vtu_path = os.path.join(results_dir, case_id, "field_final.vtu")
    residuals_path = os.path.join(results_dir, case_id, "residuals.csv")
    forces_path = os.path.join(results_dir, case_id, "forces.csv")
    surface_path = os.path.join(results_dir, case_id, "surface.csv")
    metadata_path = os.path.join(results_dir, case_id, "metadata.json")
    mach_fig = os.path.join(report_dir, "figures", f"{case_id}_mach.png")
    pressure_fig = os.path.join(report_dir, "figures", f"{case_id}_pressure.png")

    # Check 1: Positive density and pressure in field
    if os.path.exists(vtu_path) and HAS_MESHIO:
        try:
            mesh = meshio.read(vtu_path)
            if "density" in mesh.cell_data:
                rho = np.asarray(mesh.cell_data["density"]).ravel()
                checks["positive_density"] = {
                    "pass": bool(np.all(rho > 0)),
                    "min_density": float(np.min(rho)),
                    "max_density": float(np.max(rho)),
                }
            else:
                checks["positive_density"] = {"pass": None, "reason": "density field not found in VTU"}

            if "pressure" in mesh.cell_data:
                p = np.asarray(mesh.cell_data["pressure"]).ravel()
                checks["positive_pressure"] = {
                    "pass": bool(np.all(p > 0)),
                    "min_pressure": float(np.min(p)),
                    "max_pressure": float(np.max(p)),
                }
            else:
                checks["positive_pressure"] = {"pass": None, "reason": "pressure field not found in VTU"}
        except Exception as e:
            checks["field_read_error"] = {"pass": False, "error": str(e)}
    else:
        checks["positive_density"] = {"pass": None, "reason": "VTU not available"}
        checks["positive_pressure"] = {"pass": None, "reason": "VTU not available"}

    # Check 2: NACA at AoA=0 should have near-zero lift
    if case_id in NACA_CASES and os.path.exists(forces_path):
        try:
            data = np.genfromtxt(forces_path, delimiter=",", names=True, deletechars="")
            if "cl" in data.dtype.names:
                # Use last 10% of steps for averaged CL
                n = len(data)
                tail = data["cl"][max(0, int(0.9 * n)):]
                mean_cl = float(np.mean(tail))
                checks["naca_near_zero_lift"] = {
                    "pass": bool(abs(mean_cl) < 0.1),
                    "mean_cl_tail": mean_cl,
                    "sample_steps": len(tail),
                }
        except Exception as e:
            checks["naca_near_zero_lift"] = {"pass": None, "error": str(e)}
    else:
        checks["naca_near_zero_lift"] = {"pass": None, "reason": "not a NACA case or no forces.csv"}

    # Check 3: Cylinder laminar has positive mean drag
    if case_id in CYLINDER_CASES and os.path.exists(forces_path):
        try:
            data = np.genfromtxt(forces_path, delimiter=",", names=True, deletechars="")
            if "cd" in data.dtype.names:
                n = len(data)
                tail = data["cd"][max(0, int(0.5 * n)):]  # skip startup
                mean_cd = float(np.mean(tail))
                checks["cylinder_positive_drag"] = {
                    "pass": bool(mean_cd > 0),
                    "mean_cd_tail": mean_cd,
                    "sample_steps": len(tail),
                }
        except Exception as e:
            checks["cylinder_positive_drag"] = {"pass": None, "error": str(e)}
    else:
        checks["cylinder_positive_drag"] = {"pass": None, "reason": "not a cylinder case or no forces.csv"}

    # Check 4: Cylinder Re200 shows nonzero unsteady lift variation
    if case_id == "cylinder_m010_laminar_re200" and os.path.exists(forces_path):
        try:
            data = np.genfromtxt(forces_path, delimiter=",", names=True, deletechars="")
            if "cl" in data.dtype.names:
                n = len(data)
                tail = data["cl"][max(0, int(0.5 * n)):]
                cl_std = float(np.std(tail))
                checks["re200_unsteady_lift"] = {
                    "pass": bool(cl_std > 1e-4),
                    "cl_std_tail": cl_std,
                    "cl_mean_tail": float(np.mean(tail)),
                    "sample_steps": len(tail),
                }
        except Exception as e:
            checks["re200_unsteady_lift"] = {"pass": None, "error": str(e)}

    # Check 5: Surface Cp varies along body walls
    if os.path.exists(surface_path):
        try:
            data = np.genfromtxt(surface_path, delimiter=",", names=True, deletechars="")
            if "cp" in data.dtype.names and len(data["cp"]) > 1:
                cp_range = float(np.max(data["cp"]) - np.min(data["cp"]))
                checks["cp_has_variation"] = {
                    "pass": bool(cp_range > 1e-6),
                    "cp_range": cp_range,
                    "cp_min": float(np.min(data["cp"])),
                    "cp_max": float(np.max(data["cp"])),
                }
        except Exception as e:
            checks["cp_has_variation"] = {"pass": None, "error": str(e)}
    else:
        checks["cp_has_variation"] = {"pass": None, "reason": "no surface.csv"}

    # Check 6: No-slip wall rows have near-zero velocity
    if os.path.exists(surface_path):
        try:
            data = np.genfromtxt(surface_path, delimiter=",", names=True, deletechars="")
            if "u" in data.dtype.names and "v" in data.dtype.names:
                vel_mag = np.sqrt(data["u"]**2 + data["v"]**2)
                max_vel = float(np.max(vel_mag))
                is_laminar = case_id in LAMINAR_CASES
                checks["no_slip_zero_velocity"] = {
                    "pass": bool(max_vel < 1e-3) if is_laminar else None,
                    "max_wall_velocity": max_vel,
                    "expected_zero": is_laminar,
                }
        except Exception as e:
            checks["no_slip_zero_velocity"] = {"pass": None, "error": str(e)}

    # Check 7: Inviscid slip-wall rows have near-zero normal velocity
    if case_id in INVISCID_CASES and os.path.exists(surface_path):
        try:
            data = np.genfromtxt(surface_path, delimiter=",", names=True, deletechars="")
            if all(f in data.dtype.names for f in ["u", "v", "nx", "ny"]):
                vn = data["u"] * data["nx"] + data["v"] * data["ny"]
                max_vn = float(np.max(np.abs(vn)))
                checks["slip_wall_zero_normal_velocity"] = {
                    "pass": bool(max_vn < 0.01),
                    "max_normal_velocity": max_vn,
                }
        except Exception as e:
            checks["slip_wall_zero_normal_velocity"] = {"pass": None, "error": str(e)}

    # Check 8: Mach and pressure figures exist
    checks["mach_figure_exists"] = {"pass": os.path.exists(mach_fig)}
    checks["pressure_figure_exists"] = {"pass": os.path.exists(pressure_fig)}

    # Check 9: Metadata exists
    if os.path.exists(metadata_path):
        try:
            with open(metadata_path) as f:
                meta = json.load(f)
            checks["metadata_exists"] = {"pass": True}
            checks["convergence_status"] = {"status": meta.get("convergence_status", "unknown")}
        except Exception:
            checks["metadata_exists"] = {"pass": False, "error": "parse error"}
    else:
        checks["metadata_exists"] = {"pass": False, "reason": "metadata.json not found"}

    return checks


def main():
    parser = argparse.ArgumentParser(description="Physics sanity checks for CFD results")
    parser.add_argument("--results-dir", required=True, help="Root results directory (e.g., solver/results/)")
    parser.add_argument("--report-dir", required=True, help="Report directory (e.g., solver/report/)")
    parser.add_argument("--output", required=True, help="Output JSON path")
    args = parser.parse_args()

    results = {}
    overall_pass = True

    for case_id in EXPECTED_CASES:
        case_dir = os.path.join(args.results_dir, case_id)
        if not os.path.isdir(case_dir):
            results[case_id] = {"case_found": False}
            print(f"WARNING: {case_id} results directory not found")
            continue

        checks = check_case(args.results_dir, case_id, args.report_dir)
        results[case_id] = checks

        # Summarize
        passed = sum(1 for c in checks.values() if isinstance(c, dict) and c.get("pass") is True)
        failed = sum(1 for c in checks.values() if isinstance(c, dict) and c.get("pass") is False)
        unknown = sum(1 for c in checks.values() if isinstance(c, dict) and c.get("pass") is None)
        print(f"  {case_id}: {passed} passed, {failed} failed, {unknown} N/A")
        if failed > 0:
            overall_pass = False

    results["_overall_pass"] = overall_pass
    results["_timestamp"] = str(np.datetime64("now"))

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as f:
        json.dump(results, f, indent=2, default=str)
    print(f"\nSanity checks written to {args.output}")
    print(f"Overall: {'ALL PASS' if overall_pass else 'SOME FAILURES'}")


if __name__ == "__main__":
    main()

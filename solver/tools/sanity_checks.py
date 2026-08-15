"""Compute report/sanity_checks.json from result directories.

Checks per OUTPUT_CONTRACT.md "Physics Sanity Gate".
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from cfdpost import col, load_case, strouhal_from_lift


def check_case(case_dir: Path, fig_entries):
    case = load_case(case_dir)
    cid = case["metadata"]["case_id"]
    out = {"case_id": cid, "checks": {}, "passed": True}

    def record(name, passed, value, detail=""):
        out["checks"][name] = {"passed": bool(passed), "value": value, "detail": detail}
        if not passed:
            out["passed"] = False

    # 1. positive density/pressure in final field
    fld = case["field"]
    rho_min = float(fld.cell_data["density"].min())
    p_min = float(fld.cell_data["pressure"].min())
    record("positive_density", rho_min > 0, rho_min)
    record("positive_pressure", p_min > 0, p_min)

    forces = case["forces"]
    cl = col(forces, "cl")
    cd = col(forces, "cd")
    surf = case["surface"]
    cp = col(surf, "cp")
    u_wall = col(surf, "u")
    v_wall = col(surf, "v")
    cf = col(surf, "cf")
    nx = col(surf, "nx")
    ny = col(surf, "ny")
    transient = case["status"]["final_physical_time"] > 0
    laminar = "laminar" in cid

    # 5. surface cp varies along the body
    cp_range = float(cp.max() - cp.min())
    record("surface_cp_varies", cp_range > 1e-3, cp_range)

    if "naca" in cid:
        tail = cl[len(cl) // 2 :]
        mean_lift = float(np.abs(np.mean(tail)))
        record("near_zero_lift_symmetry", mean_lift < 0.02, mean_lift,
               "mean |cl| over second half")
        record("nontrivial_drag", float(np.abs(np.mean(cd[len(cd) // 2 :]))) > 1e-4,
               float(np.mean(cd[len(cd) // 2 :])))
    if "cylinder" in cid and not transient:
        mean_cd = float(np.mean(cd[len(cd) // 2 :]))
        record("positive_drag", mean_cd > 0.0, mean_cd)
    if transient:
        t = col(forces, "physical_time")
        t0 = 0.5 * t[-1]
        mask = t >= t0
        lift_std = float(np.std(cl[mask]))
        record("unsteady_lift_variation", lift_std > 1e-3, lift_std,
               "std(cl) over second half of run")
        f_peak, st, amp, _ = strouhal_from_lift(t, cl, t0)
        out["shedding"] = {
            "peak_frequency": f_peak,
            "strouhal": st,
            "lift_amplitude": amp,
            "mean_cd": float(np.mean(cd[mask])),
            "cl_amplitude_rms": float(np.sqrt(np.mean((cl[mask] - cl[mask].mean()) ** 2))),
            "analysis_window": [float(t0), float(t[-1])],
        }
        mean_cd = float(np.mean(cd[mask]))
        record("positive_drag", mean_cd > 0.0, mean_cd, "mean drag past transient")

    if laminar:
        wall_speed = float(np.sqrt(u_wall**2 + v_wall**2).max())
        record("noslip_wall_velocity_near_zero", wall_speed < 1e-8, wall_speed)
        cf_mag = float(np.abs(cf).max())
        record("nonzero_skin_friction", cf_mag > 1e-4, cf_mag)
    else:
        un = np.abs(u_wall * nx + v_wall * ny)
        record("slip_wall_normal_velocity_near_zero", float(un.max()) < 0.05,
               float(un.max()))
        vd = np.abs(col(forces, "viscous_drag")[-100:]).max()
        vl = np.abs(col(forces, "viscous_lift")[-100:]).max()
        record("inviscid_negligible_viscous_forces", max(vd, vl) < 1e-8,
               float(max(vd, vl)))

    # 9. mach + pressure figures present in manifest
    figs = {f["figure_file"]: f for f in fig_entries if f["case_id"] == cid}
    has_mach = any("mach" in f["figure_file"] and "mach" in f["variable"]
                   for f in figs.values())
    has_p = any("pressure" in f["figure_file"] and "pressure" in f["variable"]
                for f in figs.values())
    record("mach_figure_mapped", has_mach, has_mach)
    record("pressure_figure_mapped", has_p, has_p)

    out["convergence_status"] = case["status"]["convergence_status"]
    out["residual_reduction_orders"] = case["status"]["residual_reduction_orders"]
    out["final_step"] = case["status"]["final_step"]
    if not out["passed"]:
        out["convergence_status_override"] = "failed"
    return out


def main():
    results_root = Path(sys.argv[1])
    report_dir = Path(sys.argv[2])
    import csv

    fig_entries = []
    fm = report_dir / "figure_manifest.csv"
    if fm.exists():
        with open(fm, newline="") as f:
            fig_entries = list(csv.DictReader(f))
    checks = []
    for d in sorted(results_root.iterdir()):
        if d.is_dir() and (d / "metadata.json").exists():
            checks.append(check_case(d, fig_entries))
    payload = {"cases": checks, "all_passed": all(c["passed"] for c in checks)}
    (report_dir / "sanity_checks.json").write_text(json.dumps(payload, indent=2))
    print(json.dumps({c["case_id"]: c["passed"] for c in checks}, indent=2))
    print("all_passed:", payload["all_passed"])


if __name__ == "__main__":
    main()

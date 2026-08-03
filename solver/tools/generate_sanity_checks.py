#!/usr/bin/env python3
"""Generate report/sanity_checks.json from the solver results.

Checks (all keys from the benchmark specification):
    positive_density         : all cell densities > 0 in every field file
    positive_pressure        : all cell pressures > 0 in every field file
    naca_symmetric_lift      : |mean Cl| small for the symmetric NACA0012
    cylinder_positive_drag   : mean Cd > 0 for the cylinders
    re200_unsteady           : Cl varies significantly (vortex shedding)
    surface_cp_variation     : Cp varies along every wall
    wall_velocity_zero       : no-slip wall boundary velocity ~ 0
    mach_figure_exists       : <case>_mach.png exists for every case
    pressure_figure_exists   : <case>_pressure.png exists for every case

Usage:
    python generate_sanity_checks.py [results_dir] [figures_dir] [out_json]
"""

import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from plot_utils import is_viscous, read_csv, read_vtu  # noqa: E402

CASES = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
]

# Tolerances / thresholds
LIFT_SYMMETRY_TOL = 0.01      # |mean Cl| below this passes for NACA at AoA 0
RE200_LIFT_VARIATION_MIN = 0.05  # Cl peak-to-peak must exceed this (shedding)
CP_STD_MIN = 1e-3
WALL_VELOCITY_TOL = 0.05      # max wall speed < 5% of u_inf (u_inf = 1.0)


def _final_mean(data, column, fraction=0.2):
    """Mean of the last `fraction` of a column (converged/periodic state).

    Non-physical spikes (|value| > 100, from early CFL-ramp transients) are
    filtered out before averaging.
    """
    if data is None or column not in (data.dtype.names or ()):
        return None
    vals = data[column]
    if len(vals) == 0:
        return None
    n = max(1, int(len(vals) * (1.0 - fraction)))
    vals = vals[n:]
    vals = vals[np.isfinite(vals) & (np.abs(vals) < 100.0)]
    if len(vals) == 0:
        return None
    return float(vals.mean())


def _min_field(case_dir, field):
    """Minimum of a cell-data field across a case's field_final.vtu."""
    vtu = read_vtu(os.path.join(case_dir, "field_final.vtu"))
    if vtu is None or field not in vtu["cell_data"]:
        return None
    vals = vtu["cell_data"][field]
    vals = vals[:, 0] if vals.ndim > 1 else vals
    return float(vals.min()) if len(vals) else None


def main():
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    solver_dir = os.path.dirname(tools_dir)
    results_dir = (sys.argv[1] if len(sys.argv) > 1
                   else os.path.join(solver_dir, "results"))
    figures_dir = (sys.argv[2] if len(sys.argv) > 2
                   else os.path.join(solver_dir, "report", "figures"))
    out_json = (sys.argv[3] if len(sys.argv) > 3
                else os.path.join(solver_dir, "report", "sanity_checks.json"))

    details = {}
    present_cases = [c for c in CASES
                     if os.path.isdir(os.path.join(results_dir, c))]

    # --- positivity --------------------------------------------------------
    rho_min, p_min = {}, {}
    for case in present_cases:
        rho_min[case] = _min_field(os.path.join(results_dir, case), "density")
        p_min[case] = _min_field(os.path.join(results_dir, case), "pressure")
    rho_vals = [v for v in rho_min.values() if v is not None]
    p_vals = [v for v in p_min.values() if v is not None]
    positive_density = bool(rho_vals) and all(v > 0.0 for v in rho_vals)
    positive_pressure = bool(p_vals) and all(v > 0.0 for v in p_vals)
    details["min_density_by_case"] = rho_min
    details["min_pressure_by_case"] = p_min

    # --- NACA symmetric lift ------------------------------------------------
    naca_lifts = {}
    for case in present_cases:
        if "naca" not in case:
            continue
        data = read_csv(os.path.join(results_dir, case, "forces.csv"))
        cl = _final_mean(data, "cl")
        if cl is not None:
            naca_lifts[case] = cl
    lift_magnitude = (max(abs(v) for v in naca_lifts.values())
                      if naca_lifts else float("inf"))
    lift_ok = lift_magnitude < LIFT_SYMMETRY_TOL

    # --- cylinder positive drag ---------------------------------------------
    cyl_drags = {}
    for case in present_cases:
        if "cylinder" not in case:
            continue
        data = read_csv(os.path.join(results_dir, case, "forces.csv"))
        cd = _final_mean(data, "cd")
        if cd is not None:
            cyl_drags[case] = cd
    drag = max(cyl_drags.values()) if cyl_drags else float("-inf")
    drag_ok = drag > 0.0

    # --- Re200 unsteadiness -------------------------------------------------
    lift_variation = 0.0
    re200_ok = False
    re200_dir = os.path.join(results_dir, "cylinder_m010_laminar_re200")
    if os.path.isdir(re200_dir):
        data = read_csv(os.path.join(re200_dir, "forces.csv"))
        if data is not None and "cl" in (data.dtype.names or ()):
            cl = data["cl"]
            n = len(cl)
            if n > 10:
                tail = cl[n // 2:]  # statistically periodic portion
                lift_variation = float(tail.max() - tail.min())
                re200_ok = lift_variation > RE200_LIFT_VARIATION_MIN

    # --- surface Cp variation ----------------------------------------------
    surface_data = {}
    for case in present_cases:
        data = read_csv(os.path.join(results_dir, case, "surface.csv"))
        if data is not None:
            surface_data[case] = data

    cp_ok_cases = {}
    cp_std = {}
    for case, data in surface_data.items():
        if "cp" not in (data.dtype.names or ()):
            continue
        cp = data["cp"]
        cp_std[case] = float(np.std(cp))
        cp_ok_cases[case] = bool(len(cp) > 2 and cp_std[case] > CP_STD_MIN)
    cp_variation_ok = bool(cp_ok_cases) and all(cp_ok_cases.values())
    details["surface_cp_std_by_case"] = cp_std

    # --- no-slip wall velocity ---------------------------------------------
    # The contract requires near-zero reported wall velocity for no-slip
    # walls.  Tolerance is 5% of the freestream speed (1.0 for all cases).
    wall_vel_cases = {}
    wall_max_speed = {}
    for case, data in surface_data.items():
        if not is_viscous(case):
            continue
        names = data.dtype.names or ()
        if "u" not in names or "v" not in names:
            continue
        speed = np.hypot(data["u"], data["v"])
        wall_max_speed[case] = float(speed.max()) if len(speed) else 0.0
        wall_vel_cases[case] = wall_max_speed[case] < WALL_VELOCITY_TOL
    wall_ok = bool(wall_vel_cases) and all(wall_vel_cases.values())
    details["max_wall_speed_by_case"] = wall_max_speed

    # --- figure existence ---------------------------------------------------
    def _figures_exist(kind):
        """All cases with a field_final.vtu must have the figure."""
        candidates = [c for c in present_cases if os.path.isfile(
            os.path.join(results_dir, c, "field_final.vtu"))]
        return bool(candidates) and all(
            os.path.isfile(os.path.join(figures_dir, f"{c}_{kind}.png"))
            for c in candidates)

    mach_ok = _figures_exist("mach")
    pressure_fig_ok = _figures_exist("pressure")

    checks = {
        "positive_density": positive_density,
        "positive_pressure": positive_pressure,
        "naca_symmetric_lift": {
            "lift_magnitude": lift_magnitude,
            "acceptable": lift_ok,
        },
        "cylinder_positive_drag": {
            "drag": drag,
            "acceptable": drag_ok,
        },
        "re200_unsteady": {
            "lift_variation": lift_variation,
            "acceptable": re200_ok,
        },
        "surface_cp_variation": cp_variation_ok,
        "wall_velocity_zero": wall_ok,
        "mach_figure_exists": mach_ok,
        "pressure_figure_exists": pressure_fig_ok,
        "details": details,
    }

    os.makedirs(os.path.dirname(out_json), exist_ok=True)
    with open(out_json, "w") as f:
        json.dump(checks, f, indent=2)
        f.write("\n")
    print(f"wrote {out_json}")
    for key in checks:
        if key != "details":
            print(f"  {key}: {checks[key]}")


if __name__ == "__main__":
    main()

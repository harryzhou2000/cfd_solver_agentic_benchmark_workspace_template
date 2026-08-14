#!/usr/bin/env python3
"""Build report/sanity_checks.json from the submitted result directories."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np

from cfd_io import parse_vtu, read_csv, read_json


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir", type=Path)
    ap.add_argument("--report-dir", type=Path,
                    default=Path(__file__).resolve().parent.parent / "report")
    args = ap.parse_args()
    results_dir = args.results_dir.resolve()
    report_dir = args.report_dir.resolve()
    report_dir.mkdir(parents=True, exist_ok=True)

    checks = {}
    for case_dir in sorted(results_dir.iterdir()):
        if not case_dir.name.startswith("final_"):
            continue
        if not (case_dir / "run_status.json").exists():
            continue
        status = read_json(case_dir / "run_status.json")
        metadata = read_json(case_dir / "metadata.json")
        case_id = str(metadata.get("case_id", case_dir.name))
        forces = read_csv(case_dir / "forces.csv")
        surface = read_csv(case_dir / "surface.csv")
        field = parse_vtu(case_dir / "field_final.vtu")
        ok = True
        reasons = []
        gates: dict[str, bool] = {}

        rho = np.asarray(field["rho"], dtype=float)
        p = np.asarray(field["p"], dtype=float)
        gates["positive_density_pressure"] = bool(
            np.all(np.isfinite(rho)) and np.all(rho > 0)
            and np.all(np.isfinite(p)) and np.all(p > 0)
        )
        if not gates["positive_density_pressure"]:
            ok = False
            reasons.append("non-positive/non-finite density or pressure")

        cp = np.asarray([float(r["cp"]) for r in surface], dtype=float)
        gates["surface_cp_variation"] = bool(
            len(cp) and (cp.max() - cp.min()) >= 1e-9
        )
        if not gates["surface_cp_variation"]:
            ok = False
            reasons.append("surface pressure coefficient is constant")

        if case_id.startswith("naca"):
            cl_last = float(forces[-1]["cl"])
            gates["naca_zero_lift"] = abs(cl_last) <= 0.05
            if not gates["naca_zero_lift"]:
                ok = False
                reasons.append(f"nonzero lift {cl_last:.4g} at zero AoA")
        elif "re200" in case_id:
            cl = np.asarray([float(r["cl"]) for r in forces], dtype=float)
            gates["re200_unsteady_lift"] = bool(
                cl.size and float(cl[-200:].std()) >= 1e-12
            )
            if not gates["re200_unsteady_lift"]:
                ok = False
                reasons.append("no unsteady lift variation after startup")
            cd_mean = float(np.asarray(
                [float(r["cd"]) for r in forces], dtype=float)[-2000:].mean())
            gates["re200_positive_mean_drag"] = cd_mean > 0.0
            if not gates["re200_positive_mean_drag"]:
                ok = False
                reasons.append(f"non-positive mean drag {cd_mean:.4g}")
        elif "re20" in case_id:
            cd_last = float(forces[-1]["cd"])
            gates["cylinder_re20_positive_drag"] = cd_last > 0.0
            if not gates["cylinder_re20_positive_drag"]:
                ok = False
                reasons.append(f"non-positive mean drag {cd_last:.4g}")

        if "laminar" in case_id or "re20" in case_id or "re200" in case_id:
            umag = np.asarray(
                [math.hypot(float(r["u"]), float(r["v"])) for r in surface]
            )
            gates["no_slip_wall_velocity_near_zero"] = bool(
                umag.size and umag.max() <= 1e-4
            )
            if not gates["no_slip_wall_velocity_near_zero"]:
                ok = False
                reasons.append(
                    f"no-slip wall velocity not near zero (max {umag.max():.2e})"
                )
            cf = np.asarray([float(r["cf"]) for r in surface], dtype=float)
            gates["skin_friction_evidence"] = bool(
                cf.size and np.abs(cf).max() >= 1e-12
            )
            if not gates["skin_friction_evidence"]:
                ok = False
                reasons.append("no skin-friction evidence on viscous wall")
        else:
            # Inviscid slip wall: negligible viscous force columns.
            visc_drag = float(forces[-1]["viscous_drag"])
            visc_lift = float(forces[-1]["viscous_lift"])
            gates["inviscid_negligible_viscous_forces"] = bool(
                abs(visc_drag) <= 1e-8 and abs(visc_lift) <= 1e-8
            )
            if not gates["inviscid_negligible_viscous_forces"]:
                ok = False
                reasons.append("nonzero viscous force in inviscid case")
            un = np.asarray(
                [float(r["u"]) * float(r["nx"]) +
                 float(r["v"]) * float(r["ny"]) for r in surface],
                dtype=float,
            )
            gates["inviscid_normal_velocity_near_zero"] = bool(
                un.size and float(np.max(np.abs(un))) <= 1e-8
            )
            if not gates["inviscid_normal_velocity_near_zero"]:
                ok = False
                reasons.append("slip-wall normal velocity not near zero")

        figures_dir = report_dir / "figures"
        gates["mach_pressure_figures_exist"] = bool(
            (figures_dir / f"{case_id}_mach.png").exists()
            and (figures_dir / f"{case_id}_pressure.png").exists()
        )
        if not gates["mach_pressure_figures_exist"]:
            ok = False
            reasons.append("mach/pressure figure files missing")

        checks[case_id] = {
            "status": status.get("convergence_status", "failed"),
            "passed": bool(ok),
            "reasons": reasons,
            "gates": gates,
        }

    with open(report_dir / "sanity_checks.json", "w") as f:
        json.dump(checks, f, indent=2)
    print(f"wrote {report_dir / 'sanity_checks.json'}")


if __name__ == "__main__":
    main()

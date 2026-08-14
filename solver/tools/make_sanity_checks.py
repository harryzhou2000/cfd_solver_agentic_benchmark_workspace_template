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
        case_id = case_dir.name
        status = read_json(case_dir / "run_status.json")
        forces = read_csv(case_dir / "forces.csv")
        surface = read_csv(case_dir / "surface.csv")
        field = parse_vtu(case_dir / "field_final.vtu")
        ok = True
        reasons = []

        rho = np.asarray(field["rho"], dtype=float)
        p = np.asarray(field["p"], dtype=float)
        if np.any(~np.isfinite(rho)) or np.any(rho <= 0):
            ok = False
            reasons.append("non-positive density in final field")
        if np.any(~np.isfinite(p)) or np.any(p <= 0):
            ok = False
            reasons.append("non-positive pressure in final field")

        cp = np.asarray([float(r["cp"]) for r in surface], dtype=float)
        if len(cp) and (cp.max() - cp.min()) < 1e-9:
            ok = False
            reasons.append("surface pressure coefficient is constant")

        if case_id.startswith("naca"):
            cl_last = float(forces[-1]["cl"])
            if abs(cl_last) > 0.05:
                ok = False
                reasons.append(f"nonzero lift {cl_last:.4g} at zero AoA")
        elif "re20" in case_id:
            cd_last = float(forces[-1]["cd"])
            if cd_last <= 0:
                ok = False
                reasons.append(f"non-positive mean drag {cd_last:.4g}")
        elif "re200" in case_id:
            cl = np.asarray([float(r["cl"]) for r in forces], dtype=float)
            if cl.size and float(cl[-200:].std()) < 1e-12:
                ok = False
                reasons.append("no unsteady lift variation after startup")

        if "laminar" in case_id or "re20" in case_id or "re200" in case_id:
            umag = np.asarray(
                [math.hypot(float(r["u"]), float(r["v"])) for r in surface]
            )
            if umag.size and umag.max() > 1e-4:
                ok = False
                reasons.append(
                    f"no-slip wall velocity not near zero (max {umag.max():.2e})"
                )
            cf = np.asarray([float(r["cf"]) for r in surface], dtype=float)
            if cf.size and np.abs(cf).max() < 1e-12:
                ok = False
                reasons.append("no skin-friction evidence on viscous wall")
        else:
            # Inviscid slip wall: negligible viscous force columns.
            visc_drag = float(forces[-1]["viscous_drag"])
            visc_lift = float(forces[-1]["viscous_lift"])
            if abs(visc_drag) > 1e-8 or abs(visc_lift) > 1e-8:
                ok = False
                reasons.append("nonzero viscous force in inviscid case")

        checks[case_id] = {
            "status": status.get("convergence_status", "failed"),
            "passed": bool(ok),
            "reasons": reasons,
            "positive_density_pressure": bool(
                np.all(rho > 0) and np.all(p > 0)
            ),
        }

    with open(report_dir / "sanity_checks.json", "w") as f:
        json.dump(checks, f, indent=2)
    print(f"wrote {report_dir / 'sanity_checks.json'}")


if __name__ == "__main__":
    main()

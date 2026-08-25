#!/usr/bin/env python3
"""Physics and contract sanity checks over all benchmark case outputs.

Writes report/sanity_checks.json with a per-case verdict and the evidence
values used. Exits nonzero if any required case is missing or failed.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import numpy as np  # noqa: E402

from cfdplot import read_csv_cols, spectral_stats  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
OUT = ROOT / "report" / "sanity_checks.json"

CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

# Physics plausibility windows (literature-informed, deliberately wide).
CD_RANGE = {
    "naca0012_m015_inviscid": (-0.005, 0.02),
    "naca0012_m080_inviscid": (0.0, 0.03),
    "naca0012_m200_inviscid": (0.02, 0.20),
    "naca0012_m015_laminar_re5000": (0.03, 0.12),
    "naca0012_m080_laminar_re5000": (0.02, 0.15),
    "naca0012_m200_laminar_re5000": (0.05, 0.30),
    "cylinder_m010_laminar_re20": (1.8, 2.4),
    "cylinder_m010_laminar_re200": (1.1, 1.6),
}


def check_case(case_id: str) -> dict:
    d = RESULTS / case_id
    entry: dict = {"case_id": case_id, "checks": {}, "status": "missing"}
    status_path = d / "run_status.json"
    if not status_path.exists():
        return entry
    status = json.loads(status_path.read_text())
    entry["status"] = status["convergence_status"]
    entry["final_step"] = status["final_step"]
    entry["wall_time_seconds"] = status["wall_time_seconds"]
    entry["residual_reduction_orders"] = status["residual_reduction_orders"]
    entry["mpi_ranks"] = status["mpi_ranks"]

    forces = read_csv_cols(d / "forces.csv")
    res = read_csv_cols(d / "residuals.csv")
    surf = read_csv_cols(d / "surface.csv")
    cd, cl = forces["cd"], forces["cl"]
    entry["cd_final"] = float(cd[-1])
    entry["cl_final"] = float(cl[-1])

    finite = bool(
        np.all(np.isfinite(res["residual_l2"]))
        and np.all(np.isfinite(cd))
        and np.all(np.isfinite(surf["cp"]))
    )
    entry["checks"]["finite_histories"] = finite

    lo, hi = CD_RANGE[case_id]
    if "re200" in case_id:
        # Statistical analysis over the post-transient window t >= 150.
        t = forces["physical_time"]
        st_cl = spectral_stats(t, cl, 150.0)
        st_cd = spectral_stats(t, cd, 150.0)
        d = 1.0  # diameter = reference length
        u = 1.0
        strouhal = st_cl["frequency"] * d / u
        entry["strouhal"] = strouhal
        entry["cl_amplitude"] = st_cl["amplitude"]
        entry["cd_mean"] = st_cd["mean"]
        entry["cd_oscillation"] = st_cd["amplitude"]
        entry["checks"]["strouhal_in_0.15_0.25"] = bool(0.15 <= strouhal <= 0.25)
        entry["checks"]["cl_amplitude_positive"] = bool(st_cl["amplitude"] > 0.05)
        entry["checks"]["cd_mean_plausible"] = bool(lo <= st_cd["mean"] <= hi)
        entry["checks"]["statistically_periodic"] = entry["status"] == "statistically_periodic"
        entry["checks"]["final_time_300"] = bool(status["final_physical_time"] >= 300.0)
    else:
        entry["checks"]["cd_plausible"] = bool(lo <= cd[-1] <= hi)
        entry["checks"]["cl_small"] = bool(abs(cl[-1]) < 0.02)
        entry["checks"]["converged"] = entry["status"] == "converged"
        entry["checks"]["residual_reduction_ge_2"] = bool(
            status["residual_reduction_orders"] >= 2.0
        )
        if "inviscid" in case_id:
            vd = float(np.abs(forces["viscous_drag"][-1]))
            vl = float(np.abs(forces["viscous_lift"][-1]))
            entry["checks"]["inviscid_zero_viscous_forces"] = vd < 1e-8 and vl < 1e-8

    entry["ok"] = all(entry["checks"].values())
    return entry


def main() -> int:
    cases = sys.argv[1:] if len(sys.argv) > 1 else CASES
    report = {"cases": [check_case(c) for c in cases]}
    report["all_ok"] = all(c["ok"] for c in report["cases"])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2) + "\n")
    for c in report["cases"]:
        print(
            f"{c['case_id']:36s} {c['status']:22s} "
            f"ok={c.get('ok', False)} checks={c.get('checks', {})}"
        )
    print("wrote", OUT)
    return 0 if report["all_ok"] else 1


if __name__ == "__main__":
    sys.exit(main())

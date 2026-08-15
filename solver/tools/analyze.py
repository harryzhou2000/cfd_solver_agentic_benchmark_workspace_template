#!/usr/bin/env python3
"""Compute per-case result statistics and the report-level sanity checks.

Reads solver/results/<case_id>/ output directories and writes:
  - report/sanity_checks.json
  - report/results_summary.json (statistics used by the report tables)
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path

import numpy as np


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def col(rows, name):
    return np.array([float(r[name]) for r in rows])


def analyze_case(case_dir: Path) -> dict:
    meta = json.loads((case_dir / "metadata.json").read_text())
    status = json.loads((case_dir / "run_status.json").read_text())
    forces = read_csv(case_dir / "forces.csv")
    residuals = read_csv(case_dir / "residuals.csv")
    surface = read_csv(case_dir / "surface.csv")
    case_id = meta["case_id"]

    cl = col(forces, "cl")
    cd = col(forces, "cd")
    step = col(forces, "step")
    time = col(forces, "physical_time")
    res_l2 = col(residuals, "residual_l2")
    sp = col(surface, "cp")
    su = col(surface, "u")
    sv = col(surface, "v")
    smach = col(surface, "mach")
    snx = col(surface, "nx")
    sny = col(surface, "ny")
    scf = col(surface, "cf")

    is_transient = meta.get("time_integrator") == "bdf2"
    n = len(cl)
    tail = max(1, n // 5)  # last 20%

    info = {
        "case_id": case_id,
        "mpi_ranks": meta["mpi_ranks"],
        "final_step": int(status["final_step"]),
        "final_physical_time": float(status["final_physical_time"]),
        "wall_time_seconds": float(status["wall_time_seconds"]),
        "convergence_status": status["convergence_status"],
        "residual_reduction_orders": float(status["residual_reduction_orders"]),
        "final_cl": float(cl[-1]),
        "final_cd": float(cd[-1]),
        "final_cmz": float(col(forces, "cmz")[-1]),
        "final_pressure_drag": float(col(forces, "pressure_drag")[-1]),
        "final_viscous_drag": float(col(forces, "viscous_drag")[-1]),
        "mean_cl_tail": float(np.mean(cl[-tail:])),
        "mean_cd_tail": float(np.mean(cd[-tail:])),
        "residual_l2_start": float(res_l2[0]),
        "residual_l2_final": float(res_l2[-1]),
        "num_cells_global": meta["num_cells_global"],
        "num_faces_global": meta["num_faces_global"],
        "partition_edge_cut": meta.get("partition_edge_cut"),
    }

    checks = {}

    # positivity of density/pressure from the field file is checked separately
    # (here: surface and force based checks)
    # NACA symmetry: near-zero lift
    if "naca" in case_id:
        checks["lift_near_zero"] = bool(abs(np.mean(cl[-tail:])) < 0.02)
        checks["cp_varies"] = bool(np.ptp(sp) > 0.1)
    # cylinder drag positive
    if "cylinder" in case_id:
        if is_transient:
            checks["mean_drag_positive"] = bool(np.mean(cd[-tail:]) > 0)
            amp = 0.5 * (np.max(cl[-tail:]) - np.min(cl[-tail:]))
            checks["unsteady_lift_nonzero"] = bool(amp > 1e-3)
            info["lift_amplitude_tail"] = float(amp)
            # Strouhal from FFT on the settled part of the signal
            t = time[-tail:]
            s = cl[-tail:]
            s = s - np.mean(s)
            dt = float(np.mean(np.diff(t)))
            if dt > 0 and len(s) > 16:
                win = np.hanning(len(s))
                S = np.fft.rfft(s * win)
                freqs = np.fft.rfftfreq(len(s), dt)
                im = np.argmax(np.abs(S[1:])) + 1
                f0 = float(freqs[im])
                info["shedding_frequency"] = f0
                info["strouhal"] = f0 * 1.0 / 1.0  # D=1, U=1
            info["mean_cd_settled"] = float(np.mean(cd[-tail:]))
        else:
            checks["mean_drag_positive"] = bool(cd[-1] > 0)
        checks["cp_varies"] = bool(np.ptp(sp) > 0.1)

    # wall boundary semantics
    if "laminar" in case_id or "re200" in case_id or "re20" in case_id:
        umag = np.hypot(su, sv)
        checks["noslip_wall_velocity_near_zero"] = bool(np.max(umag) < 1e-6)
        checks["noslip_mach_near_zero"] = bool(np.max(smach) < 1e-6)
        checks["skin_friction_nonzero"] = bool(np.max(np.abs(scf)) > 1e-5)
    if "inviscid" in case_id:
        un = np.abs(su * snx + sv * sny)
        checks["slip_wall_normal_velocity_near_zero"] = bool(np.max(un) < 1e-4)
        checks["inviscid_viscous_forces_zero"] = bool(
            abs(float(col(forces, "viscous_drag")[-1])) < 1e-12
            and abs(float(col(forces, "viscous_lift")[-1])) < 1e-12)

    info["checks"] = checks
    info["all_checks_passed"] = bool(all(checks.values())) if checks else True
    return info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", type=Path, default=Path("results"))
    ap.add_argument("--report", type=Path, default=Path("report"))
    args = ap.parse_args()

    summary = []
    for d in sorted(args.results.iterdir()):
        if (d / "metadata.json").exists() and (d / "run_status.json").exists():
            try:
                summary.append(analyze_case(d))
            except Exception as e:  # noqa: BLE001
                print(f"WARN: failed to analyze {d}: {e}")

    args.report.mkdir(parents=True, exist_ok=True)
    (args.report / "results_summary.json").write_text(json.dumps(summary, indent=1))

    sanity = {}
    for s in summary:
        entry = dict(s["checks"])
        entry["status"] = s["convergence_status"]
        entry["all_checks_passed"] = s["all_checks_passed"]
        sanity[s["case_id"]] = entry
    (args.report / "sanity_checks.json").write_text(json.dumps(sanity, indent=1))

    for s in summary:
        print(f"{s['case_id']:36s} status={s['convergence_status']:22s} "
              f"cd={s['final_cd']:.5f} cl={s['final_cl']:.6f} checks={'OK' if s['all_checks_passed'] else 'FAIL'}")

    # run manifest
    lines = ["# Run manifest", "",
             "| case | command | ranks | wall [s] | final step | final t | status | residual orders |",
             "|---|---|---|---|---|---|---|---|"]
    for s in summary:
        st = json.loads((args.results / s["case_id"] / "run_status.json").read_text())
        t = "--" if st["final_physical_time"] == 0 else f"{st['final_physical_time']:.4g}"
        lines.append(
            f"| `{s['case_id']}` | `{st['command']}` | {st['mpi_ranks']} | "
            f"{st['wall_time_seconds']:.1f} | {st['final_step']} | {t} | "
            f"{st['convergence_status']} | {st['residual_reduction_orders']:.3g} |")
    lines.append("")
    lines.append("All steady cases use the supplied production CFL schedules and inner-iteration "
                 "bounds. The Re 200 case uses the supplied dt=0.01, final_time=300, 5-1000 inner "
                 "iterations, inner target 1e-3, Rusanov dissipation scale 1.0 with the documented "
                 "low-Mach dissipation fix (see report).")
    (args.report / "run_manifest.md").write_text("\n".join(lines) + "\n")
    print("wrote", args.report / "run_manifest.md")


if __name__ == "__main__":
    main()

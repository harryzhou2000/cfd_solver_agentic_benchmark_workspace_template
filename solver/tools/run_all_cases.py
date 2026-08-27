#!/usr/bin/env python3
"""Run benchmark cases with the fv2d solver and record a run manifest.

Usage: python tools/run_all_cases.py [--np 8] [--cases case1,case2] [--tag NAME]
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

SOLVER_ROOT = Path(__file__).resolve().parent.parent
BENCH = SOLVER_ROOT.parent / "cfd_solver_agentic_benchmark"
CASES_DIR = BENCH / "inputs" / "cases"
RESULTS = SOLVER_ROOT / "results"
FV2D = SOLVER_ROOT / "build" / "fv2d"

ALL_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


# Production per-case numerical settings (documented in report sec. limitations).
# These are runtime CLI/env options, not solver-code edits; the solver binary is
# identical for all cases.
PER_CASE = {
    "cylinder_m010_laminar_re20": {"flux": "rusanov", "env": {"FV2D_VENKAT": "1.0"},
                                    "cfl_max": 30.0},  # conservative cap for stiff low-Mach case
    # Deep-convergence laminar NACA settings: these cases settle to massively
    # separated states whose force coefficients drift slowly long after the
    # case residual target is met; production therefore uses a longer horizon
    # with a higher target so the run ends on the force plateau (documented in
    # report sec. limitations). CFL caps are conservative stability equivalents.
    "naca0012_m080_laminar_re5000": {"flux": "roe",
                                     "env": {"FV2D_VENKAT": "1.0", "FV2D_NOEXTEND": "1"},
                                     "np": 2,
                                     "rc": {"max_steps": 25000, "residual_reduction_target": 6.0,
                                            "cfl_initial": 0.5, "cfl_max": 50.0,
                                            "pseudo_cfl_ramp_steps": 8000}},
    # Mach-2 laminar is the stiffest case: Roe+Barth enters a late limit-cycle
    # instability near the separated plateau, so production uses the documented
    # stiff-case recipe (Rusanov + Venkatakrishnan, conservative CFL cap).
    "naca0012_m200_laminar_re5000": {"flux": "rusanov",
                                     "env": {"FV2D_VENKAT": "1.0", "FV2D_NOEXTEND": "1"},
                                     "np": 2,
                                     "rc": {"max_steps": 20000, "residual_reduction_target": 6.0,
                                            "cfl_initial": 0.2, "cfl_max": 10.0,
                                            "pseudo_cfl_ramp_steps": 3000}},
    "cylinder_m010_laminar_re200": {"flux": "rusanov"},  # transient production flux
}


def _variant_case(case_id, rc_overrides):
    """Write a case JSON identical to the supplied one but with documented
    run-control overrides (e.g. a conservative pseudo-CFL cap chosen for
    stability on a stiff case, or a longer horizon to reach a force plateau).
    rc_overrides may be a float (cfl_max only, legacy form) or a dict of
    run_control keys."""
    d = json.load(open(CASES_DIR / f"{case_id}.json"))
    mesh = d["mesh"]["file"]
    if not mesh.startswith("/"):
        d["mesh"]["file"] = str((CASES_DIR.parent / "meshes" / Path(mesh).name).resolve())
    if isinstance(rc_overrides, dict):
        d["run_control"].update(rc_overrides)
    else:
        d["run_control"]["cfl_max"] = rc_overrides
    out = SOLVER_ROOT / "tests" / "cases" / f"{case_id}_production.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    json.dump(d, open(out, "w"), indent=2)
    return out


def run_case(case_id, np_, tag=None, extra_env=None):
    out = RESULTS / (case_id if tag is None else f"{case_id}_np{np_}" if tag == "np" else f"{case_id}_{tag}")
    out.mkdir(parents=True, exist_ok=True)
    pc = PER_CASE.get(case_id, {})
    if tag is None and "np" in pc:
        np_ = pc["np"]
    rc_ov = pc.get("rc", pc.get("cfl_max"))
    case_path = _variant_case(case_id, rc_ov) if rc_ov is not None else (CASES_DIR / f"{case_id}.json")
    cmd = ["mpirun", "-np", str(np_), str(FV2D), "solve", "--case",
           str(case_path), "--output", str(out)]
    if pc.get("flux"):
        cmd += ["--flux", pc["flux"]]
    env = dict(os.environ)
    env.update(pc.get("env", {}))
    if extra_env:
        env.update(extra_env)
    t0 = time.time()
    with open(out / "runner.log", "w") as logf:
        proc = subprocess.run(cmd, stdout=logf, stderr=subprocess.STDOUT, env=env)
    wall = time.time() - t0
    status = "unknown"
    conv = ""
    try:
        st = json.load(open(out / "run_status.json"))
        status = st["convergence_status"]
        conv = st.get("residual_reduction_orders", "")
    except Exception:
        status = "failed_no_status"
    return {
        "case_id": case_id,
        "output_dir": str(out),
        "command": " ".join(cmd),
        "mpi_ranks": np_,
        "wall_time_seconds": round(wall, 1),
        "convergence_status": status,
        "residual_reduction_orders": conv,
        "exit_code": proc.returncode,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--np", type=int, default=8)
    ap.add_argument("--cases", type=str, default=",".join(ALL_CASES))
    ap.add_argument("--tag", type=str, default=None)
    ap.add_argument("--manifest", type=str, default=str(SOLVER_ROOT / "report" / "run_manifest.csv"))
    args = ap.parse_args()
    rows = []
    for cid in args.cases.split(","):
        print(f"[run_all] {cid} np={args.np}", flush=True)
        row = run_case(cid, args.np, args.tag)
        print("[run_all]   ->", row["convergence_status"], row["wall_time_seconds"], "s", flush=True)
        rows.append(row)
    # append to manifest
    import csv
    mpath = Path(args.manifest)
    mpath.parent.mkdir(parents=True, exist_ok=True)
    write_header = not mpath.exists()
    with open(mpath, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["case_id", "command", "mpi_ranks", "wall_time_seconds",
                                          "convergence_status", "residual_reduction_orders", "exit_code",
                                          "output_dir"])
        if write_header:
            w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"[run_all] manifest updated: {mpath}")


if __name__ == "__main__":
    main()

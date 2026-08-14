#!/usr/bin/env python3
"""Physics sanity gate -> report/sanity_checks.json (see OUTPUT_CONTRACT.md)."""
import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from vtu_dump import read_vtu


def read_csv(path):
    with open(path) as fh:
        return list(csv.DictReader(fh))


def col(rows, name, dtype=float):
    return np.array([dtype(r[name]) for r in rows])


def check_case(cd: Path):
    cid = cd.name
    out = {"case_id": cid, "checks": {}, "passed": True}

    def record(name, ok, detail):
        out["checks"][name] = {"passed": bool(ok), "detail": detail}
        if not ok:
            out["passed"] = False

    f = read_vtu(str(cd / "field_final.vtu"))
    rho = f["density"]
    p = f["pressure"]
    record("positive_density_pressure", np.all(rho > 0) and np.all(p > 0),
           f"min rho {rho.min():.4g}, min p {p.min():.4g}")

    forces = read_csv(cd / "forces.csv")
    surf = read_csv(cd / "surface.csv")
    cl = col(forces, "cl")
    cdv = col(forces, "cd")
    cp = col(surf, "cp")
    u = col(surf, "u")
    v = col(surf, "v")
    nx = col(surf, "nx")
    ny = col(surf, "ny")
    cf = col(surf, "cf")

    cp_range = float(cp.max() - cp.min())
    record("surface_cp_varies", cp_range > 1e-3, f"cp range {cp_range:.4g}")

    if "naca" in cid:
        record("near_zero_lift_symmetry", abs(cl[-1]) < 0.02, f"final cl {cl[-1]:.5f}")
        record("nontrivial_drag", abs(cdv[-1]) > 1e-5, f"final cd {cdv[-1]:.5f}")
    if "cylinder" in cid:
        if "re200" in cid:
            n = len(cl) // 3  # post-startup window
            record("positive_mean_drag", cdv[n:].mean() > 0, f"mean cd (last 2/3) {cdv[n:].mean():.4f}")
            cl_var = float(cl[n:].std())
            record("unsteady_lift_variation", cl_var > 1e-4, f"cl std (last 2/3) {cl_var:.4f}")
        else:
            record("positive_mean_drag", cdv[-1] > 0, f"final cd {cdv[-1]:.4f}")
    meta = json.loads((cd / "metadata.json").read_text())
    laminar = meta.get("viscous_flux", "none") != "none"
    if laminar:
        umag = np.sqrt(u**2 + v**2)
        record("no_slip_wall_zero_velocity", float(umag.max()) < 1e-6,
               f"max |u| on wall rows {umag.max():.3g}")
        record("nonzero_skin_friction", float(np.abs(cf).max()) > 1e-6,
               f"max |cf| {np.abs(cf).max():.4g}")
    else:
        un = np.abs(u * nx + v * ny)
        record("slip_wall_zero_normal_velocity", float(un.max()) < 1e-6,
               f"max |u_n| on wall rows {un.max():.3g}")
        vd = np.abs(col(forces, "viscous_drag"))[-1]
        vl = np.abs(col(forces, "viscous_lift"))[-1]
        record("inviscid_negligible_viscous_forces", max(vd, vl) < 1e-8,
               f"viscous_drag {vd:.3g}, viscous_lift {vl:.3g}")

    status = json.loads((cd / "run_status.json").read_text())
    record("completed_status", status["convergence_status"] in ("converged", "statistically_periodic"),
           status["convergence_status"])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--figures", default="")
    ap.add_argument("--manifest", default="")
    args = ap.parse_args()

    results = Path(args.results)
    report = {"cases": [], "figures": {}, "all_passed": True}
    for cd in sorted(results.iterdir()):
        if not (cd / "metadata.json").exists():
            continue
        r = check_case(cd)
        report["cases"].append(r)
        if not r["passed"]:
            report["all_passed"] = False

    if args.manifest and Path(args.manifest).exists():
        entries = list(csv.DictReader(open(args.manifest)))
        by_case = {}
        for e in entries:
            by_case.setdefault(e["case_id"], set()).add(e["variable"].lower())
        for r in report["cases"]:
            cid = r["case_id"]
            vars_ = by_case.get(cid, set())
            ok = "mach" in vars_ and "pressure" in vars_
            r["checks"]["mach_and_pressure_figures"] = {
                "passed": ok,
                "detail": f"manifest variables: {sorted(vars_)}",
            }
            if not ok:
                r["passed"] = False
                report["all_passed"] = False

    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(report, indent=2))
    print(f"sanity checks: all_passed={report['all_passed']} -> {args.out}")
    for r in report["cases"]:
        bad = [k for k, v in r["checks"].items() if not v["passed"]]
        print(f"  {r['case_id']}: {'PASS' if r['passed'] else 'FAIL ' + ','.join(bad)}")


if __name__ == "__main__":
    main()

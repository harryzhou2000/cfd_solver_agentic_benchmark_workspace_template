#!/usr/bin/env python3
"""Machine-readable physics sanity checks over all completed case outputs.

usage: sanity_checks.py <results-root> <cases-dir> <out-json>
"""
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cfdplot import read_vtu


def check_case(case_dir, case):
    cid = case["case_id"]
    out = {"case_id": cid, "checks": {}, "passed": True}

    def fail(name, msg):
        out["checks"][name] = {"pass": False, "detail": msg}
        out["passed"] = False

    def ok(name, msg):
        out["checks"][name] = {"pass": True, "detail": msg}

    try:
        pts, tris, cellidx, fields = read_vtu(
            os.path.join(case_dir, "field_final.vtu"))
    except Exception as e:
        fail("field_readable", str(e))
        return out
    rho = fields["Density"]
    p = fields["Pressure"]
    if rho.min() > 0 and p.min() > 0:
        ok("positivity", f"min rho={rho.min():.4g}, min p={p.min():.4g}")
    else:
        fail("positivity", f"min rho={rho.min():.4g}, min p={p.min():.4g}")

    foc = np.genfromtxt(os.path.join(case_dir, "forces.csv"),
                        delimiter=",", names=True)
    srf = np.genfromtxt(os.path.join(case_dir, "surface.csv"),
                        delimiter=",", names=True, dtype=None,
                        encoding=None)
    meta = json.load(open(os.path.join(case_dir, "metadata.json")))
    run = case["run_control"]
    transient = run["type"] == "transient"
    viscous = case["physics"]["mode"] == "laminar"
    is_cyl = "cylinder" in cid

    cp = srf["cp"]
    if cp.max() - cp.min() > 1e-3:
        ok("surface_cp_variation",
           f"cp range [{cp.min():.3f}, {cp.max():.3f}]")
    else:
        fail("surface_cp_variation", "cp constant along wall")

    umag = np.hypot(srf["u"], srf["v"])
    if viscous:
        if umag.max() < 1e-3:
            ok("noslip_wall_velocity", f"max wall speed {umag.max():.3g}")
        else:
            fail("noslip_wall_velocity", f"max wall speed {umag.max():.3g}")
        if np.abs(srf["cf"]).max() > 1e-6:
            ok("skin_friction_present",
               f"max |cf| {np.abs(srf['cf']).max():.3g}")
        else:
            fail("skin_friction_present", "cf identically zero")
    else:
        unn = np.abs(srf["u"] * srf["nx"] + srf["v"] * srf["ny"])
        if unn.max() < 2e-2:
            ok("slip_wall_normal_velocity",
               f"max |u_n| {unn.max():.3g}")
        else:
            fail("slip_wall_normal_velocity", f"max |u_n| {unn.max():.3g}")
        vd = np.abs(foc["viscous_drag"]).max()
        vl = np.abs(foc["viscous_lift"]).max()
        if vd < 1e-8 and vl < 1e-8:
            ok("inviscid_viscous_forces_zero", f"max |vd| {vd:.2g}")
        else:
            fail("inviscid_viscous_forces_zero", f"vd {vd:.2g} vl {vl:.2g}")

    if transient:
        t = foc["physical_time"]
        n0 = int(0.5 * len(t))
        cl_tail = foc["cl"][n0:]
        if cl_tail.max() - cl_tail.min() > 0.02:
            ok("unsteady_lift", f"cl tail amplitude {cl_tail.max()-cl_tail.min():.3f}")
        else:
            fail("unsteady_lift",
                 f"cl tail amplitude {cl_tail.max()-cl_tail.min():.3f}")
        cd_tail = foc["cd"][n0:]
        if cd_tail.mean() > 0.1:
            ok("positive_mean_drag", f"mean cd {cd_tail.mean():.3f}")
        else:
            fail("positive_mean_drag", f"mean cd {cd_tail.mean():.3f}")
    else:
        cd_last = foc["cd"][-1]
        if is_cyl:
            if cd_last > 0.1:
                ok("positive_drag", f"final cd {cd_last:.3f}")
            else:
                fail("positive_drag", f"final cd {cd_last:.3f}")
        else:
            cl_last = foc["cl"][-1]
            if abs(cl_last) < 0.05:
                ok("near_zero_lift_symmetry", f"final cl {cl_last:.4f}")
            else:
                fail("near_zero_lift_symmetry", f"final cl {cl_last:.4f}")

    if transient and not meta.get("true_bdf2_inner_loop", False):
        fail("bdf2_inner_loop_flag", "metadata missing true_bdf2_inner_loop")
    if not out["passed"]:
        out["recommended_status"] = "failed"
    else:
        out["recommended_status"] = meta["convergence_status"]
    return out


def main():
    root, cases_dir, out_json = sys.argv[1], sys.argv[2], sys.argv[3]
    results = {}
    allpass = True
    for cj in sorted(glob.glob(os.path.join(cases_dir, "*.json"))):
        case = json.load(open(cj))
        cid = case["case_id"]
        cdir = os.path.join(root, cid)
        if not os.path.isdir(cdir):
            results[cid] = {"case_id": cid, "passed": False,
                            "checks": {}, "recommended_status": "missing"}
            allpass = False
            continue
        r = check_case(cdir, case)
        results[cid] = r
        allpass = allpass and r["passed"]
    summary = {"all_passed": allpass, "cases": results}
    with open(out_json, "w") as fh:
        json.dump(summary, fh, indent=1)
    print(f"sanity checks: all_passed={allpass} -> {out_json}")


if __name__ == "__main__":
    main()

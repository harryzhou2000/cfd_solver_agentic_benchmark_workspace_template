#!/usr/bin/env python3
"""Generate report/sanity_checks.json -- the machine-readable physics gate.

Usage:
    python3 tools/gen_sanity.py results/naca_test results/naca_lam ...
    python3 tools/gen_sanity.py --auto

Implements the sanity gate from OUTPUT_CONTRACT.md / SCORING_RUBRIC.md:
  1. positive density and pressure in every completed field
  2. NACA 0-deg AoA: near-zero lift, non-trivial cd/cp/field
  3. cylinder laminar: positive mean drag after startup
  4. cylinder Re200: nonzero unsteady lift variation (and bounded)
  5. surface cp varies along walls
  6. no-slip wall: near-zero wall velocity (+ skin friction when viscous)
  7. inviscid slip wall: near-zero normal velocity, negligible viscous force
  8. cases failing a check are recommended 'failed'
  9. mach/pressure figures present and mapped (when figure_manifest exists)
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import sys
from datetime import datetime, timezone

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

# ---- thresholds -------------------------------------------------------------
POS_FLOOR = 0.0                 # density/pressure must be strictly > 0
CP_STD_PASS = 1e-6
CP_STD_FAIL = 1e-9
NACA_LIFT_PASS = 1e-2           # |final cl| < this -> pass (near zero)
NACA_LIFT_WARN = 1e-1           # < this -> warn; above -> fail
CYL_RE20_DRAG_TOL = 1e-9        # mean drag must exceed this
CYL_RE200_CL_STD_MIN = 1e-3     # nonzero unsteady lift variation
CYL_RE200_CL_BOUND = 10.0       # |cl| late must stay below this (not diverging)
CYL_RE200_T_TARGET = 300.0
CYL_RE200_T_FAIL = 10.0         # final_time below this -> incomplete (fail)
NOSLIP_VEL_MAX = 1e-6
NOSLIP_CF_MIN = 1e-9
SLIP_VN_MAX = 1e-4              # relative-to-freestream numerical residual; 1e-6 false-fails
VISCOUS_FORCE_MAX = 1e-6
INNER_CONV_FAIL = 0.0
INNER_CONV_WARN = 0.5
RESIDUAL_BLOWUP = 1e6          # residual magnitude above this -> clear blow-up (fail)
RESIDUAL_GROWTH_RATIO = 100.0  # late/initial growth above this (without blow-up) -> warn

THRESHOLDS = {
    "density_pressure_positive": POS_FLOOR,
    "cp_std_pass": CP_STD_PASS,
    "naca_lift_pass": NACA_LIFT_PASS,
    "naca_lift_warn": NACA_LIFT_WARN,
    "cyl_re200_cl_std_min": CYL_RE200_CL_STD_MIN,
    "cyl_re200_cl_bound": CYL_RE200_CL_BOUND,
    "cyl_re200_time_target": CYL_RE200_T_TARGET,
    "noslip_velocity_max": NOSLIP_VEL_MAX,
    "slip_normal_velocity_max": SLIP_VN_MAX,
    "viscous_force_max": VISCOUS_FORCE_MAX,
}


def _safe_float(x):
    """Coerce non-finite floats to None so JSON output is strict (allow_nan=False)."""
    if isinstance(x, float) and (x != x or x in (float("inf"), float("-inf"))):
        return None
    return x


def _sanitize(obj):
    """Recursively replace NaN/Inf floats with None for strict JSON."""
    if isinstance(obj, dict):
        return {k: _sanitize(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [_sanitize(v) for v in obj]
    return _safe_float(obj)


def _check(status, value, threshold=None, details=""):
    return {"status": status, "value": value, "threshold": threshold, "details": details}


def _nonfinite_count(arr):
    if arr is None or arr.size == 0:
        return 0
    return int(np.sum(~np.isfinite(arr)))


def field_positivity(field):
    rho = field.get("rho"); p = field.get("p")
    if rho is None or p is None or rho.size == 0 or p.size == 0:
        return _check("fail", {"n_cells": 0}, {"min_rho_gt": POS_FLOOR, "min_p_gt": POS_FLOOR},
                      "density/pressure field missing")
    n = rho.size
    n_neg_rho = int(np.sum(rho <= 0))
    n_neg_p = int(np.sum(p <= 0))
    ok = bool(np.nanmin(rho) > POS_FLOOR and np.nanmin(p) > POS_FLOOR)
    val = {
        "n_cells": int(n),
        "min_density": float(np.nanmin(rho)), "max_density": float(np.nanmax(rho)),
        "min_pressure": float(np.nanmin(p)), "max_pressure": float(np.nanmax(p)),
        "n_negative_density": n_neg_rho, "n_negative_pressure": n_neg_p,
    }
    det = (f"min rho={val['min_density']:.6g}, min p={val['min_pressure']:.6g}; "
           f"{n_neg_p} cell(s) with p<=0, {n_neg_rho} with rho<=0")
    return _check("pass" if ok else "fail", val,
                  {"min_rho_gt": POS_FLOOR, "min_p_gt": POS_FLOOR}, det)


def field_finite(field):
    vals = {k: field.get(k) for k in ("rho", "u", "v", "p", "mach", "T")}
    counts = {k: _nonfinite_count(v) for k, v in vals.items()}
    missing = [k for k, v in vals.items() if v is None or v.size == 0]
    bad = sum(counts.values())
    ok = bad == 0 and not missing
    if missing:
        det = f"missing field array(s): {missing}"
    else:
        det = f"{bad} non-finite value(s) across rho/u/v/p/mach/T"
    return _check("pass" if ok else "fail", {"nonfinite_counts": counts},
                  {"nonfinite_total": 0, "required_arrays": ["rho","u","v","p","mach","T"]}, det)


def field_nontrivial(field):
    mach = field.get("mach"); p = field.get("p"); rho = field.get("rho")
    rng = {}
    for k, a in (("mach", mach), ("pressure", p), ("density", rho)):
        rng[k] = float(np.nanmax(a) - np.nanmin(a)) if a is not None and a.size else 0.0
    nontrivial = rng["mach"] > 1e-6 or rng["pressure"] > 1e-6 or rng["density"] > 1e-6
    # extremely large ranges (blow-up) are NOT a pass
    blowup = rng["mach"] > 1e3 or rng["pressure"] > 1e5
    if blowup:
        st = "fail"
        det = "field ranges are extreme (likely blow-up)"
    elif nontrivial:
        st = "pass"
        det = "field varies (non-trivial solution)"
    else:
        st = "fail"
        det = "field is (near) constant -- trivial solution"
    return _check(st, {"mach_range": rng["mach"], "pressure_range": rng["pressure"],
                       "density_range": rng["density"]},
                  {"mach_range_min": 1e-6}, det)


def surface_cp_variation(surf):
    if surf is None:
        return _check("skip", None, None, "surface.csv missing")
    cp = surf.get("cp")
    if cp is None or cp.size == 0:
        return _check("fail", None, {"cp_std_min": CP_STD_PASS}, "cp column missing")
    std = float(np.nanstd(cp))
    val = {"n_wall_rows": int(cp.size), "cp_min": float(np.nanmin(cp)),
           "cp_max": float(np.nanmax(cp)), "cp_std": std}
    if std > CP_STD_PASS:
        st = "pass"
    elif std > CP_STD_FAIL:
        st = "warn"
    else:
        st = "fail"
    return _check(st, val, {"cp_std_pass": CP_STD_PASS, "cp_std_fail": CP_STD_FAIL},
                  f"cp std={std:.6g} over {val['n_wall_rows']} wall rows")


def naca_lift(forces, final_row):
    if final_row is None or "cl" not in final_row:
        return _check("skip", None, None, "no final force row")
    cl = float(final_row["cl"])
    acl = abs(cl)
    if acl < NACA_LIFT_PASS:
        st = "pass"
    elif acl < NACA_LIFT_WARN:
        st = "warn"
    else:
        st = "fail"
    return _check(st, {"final_cl": cl, "abs_final_cl": acl},
                  {"pass_lt": NACA_LIFT_PASS, "warn_lt": NACA_LIFT_WARN},
                  f"symmetric airfoil 0-deg AoA: |final cl|={acl:.6g}")


def naca_nontrivial(surf, field):
    cp_std = float(np.nanstd(surf["cp"])) if (surf and "cp" in surf and surf["cp"].size) else 0.0
    mach_rng = 0.0
    if field.get("mach") is not None and field["mach"].size:
        mach_rng = float(np.nanmax(field["mach"]) - np.nanmin(field["mach"]))
    ok = cp_std > 1e-6 and mach_rng > 1e-6
    st = "pass" if ok else "warn"
    return _check(st, {"cp_std": cp_std, "mach_range": mach_rng},
                  {"cp_std_min": 1e-6, "mach_range_min": 1e-6},
                  "drag/cp/field must not be identically trivial (inviscid drag~0 is allowed)")


def cyl_re20_drag(forces):
    if forces is None or "cd" not in forces or forces["cd"].size == 0:
        return _check("skip", None, None, "forces.csv missing")
    cd = forces["cd"]
    idx = C.late_indices(cd.size)
    late = cd[idx]
    mean_cd = float(np.nanmean(late)) if late.size else 0.0
    if mean_cd > CYL_RE20_DRAG_TOL:
        st = "pass"
    elif mean_cd > 0:
        st = "warn"
    else:
        st = "fail"
    return _check(st, {"mean_cd_late": mean_cd, "n_late": int(late.size),
                       "min_cd_late": float(np.nanmin(late)) if late.size else 0.0,
                       "max_cd_late": float(np.nanmax(late)) if late.size else 0.0},
                  {"mean_cd_gt": CYL_RE20_DRAG_TOL},
                  f"mean drag (late)={mean_cd:.6g} over {late.size} samples")


def cyl_re200_lift(forces):
    if forces is None or "cl" not in forces or forces["cl"].size == 0:
        return _check("fail", None, None, "forces.csv missing for Re200")
    cl = forces["cl"]
    idx = C.late_indices(cl.size)
    late = cl[idx]
    if not late.size or not np.all(np.isfinite(late)):
        return _check("fail", {"n_late": int(late.size)},
                      {"finite_required": True},
                      "non-finite or empty lift history (corrupt/diverged forces.csv)")
    std = float(np.nanstd(late)) if late.size else 0.0
    maxabs = float(np.nanmax(np.abs(late))) if late.size else 0.0
    mean = float(np.nanmean(late)) if late.size else 0.0
    if maxabs > CYL_RE200_CL_BOUND:
        st = "fail"
        det = f"lift is diverging: max|cl|={maxabs:.6g} > bound {CYL_RE200_CL_BOUND}"
    elif std < CYL_RE200_CL_STD_MIN:
        st = "fail"
        det = f"no unsteady lift variation: std(cl)={std:.6g} < {CYL_RE200_CL_STD_MIN}"
    else:
        st = "pass"
        det = f"unsteady shedding: std(cl)={std:.6g}, max|cl|={maxabs:.6g}"
    return _check(st, {"std_cl_late": std, "max_abs_cl_late": maxabs,
                       "mean_cl_late": mean, "n_late": int(late.size)},
                  {"cl_std_min": CYL_RE200_CL_STD_MIN, "cl_bound": CYL_RE200_CL_BOUND}, det)


def residual_terminal_trend(residuals_path):
    """Assess the residual L2 terminal trend for clear divergence (blow-up).

    A *blow-up* (late residual magnitude above RESIDUAL_BLOWUP, e.g. the diverged
    Re 200 run whose L2 reaches 1e7) is a hard fail.  Large-but-sub-blow-up growth
    is a warn rather than a fail, because this solver's L2 norm is a density norm
    that is not guaranteed to decrease monotonically even for physically-converged
    cases; failing on trend alone would false-fail legitimately plateaued cases.
    """
    rcols = C.load_forces(residuals_path) if residuals_path else None
    if rcols is None or "residual_l2" not in rcols or rcols["residual_l2"].size == 0:
        return _check("skip", None, None, "residuals.csv missing or no residual_l2 column")
    r = rcols["residual_l2"]
    r = r[np.isfinite(r)]
    if r.size < 2:
        return _check("skip", {"n": int(r.size)}, {"min_points": 2},
                      "too few residual points to assess trend")
    init = float(np.nanmedian(r[:max(1, r.size // 10)])) if r.size else 1.0
    init = init if init > 0 else 1.0
    idx = C.late_indices(r.size)
    late = r[idx] if idx.size else r
    late_max = float(np.nanmax(late))
    late_mean = float(np.nanmean(late))
    if late_max > RESIDUAL_BLOWUP:
        st = "fail"
        det = f"residual blow-up: late max L2={late_max:.6g} > {RESIDUAL_BLOWUP}"
    elif late_max > init * RESIDUAL_GROWTH_RATIO:
        st = "warn"
        det = (f"residual growing: late max L2={late_max:.6g} > "
               f"{RESIDUAL_GROWTH_RATIO:g}*initial median({init:.6g})")
    else:
        st = "pass"
        det = (f"stable terminal trend: late mean L2={late_mean:.6g} "
               f"(init median={init:.6g}, ratio={late_max/init:.4g})")
    return _check(st, {"initial_l2": init, "late_mean_l2": late_mean,
                       "late_max_l2": late_max, "n_late": int(late.size)},
                  {"growth_warn_ratio": RESIDUAL_GROWTH_RATIO, "blowup": RESIDUAL_BLOWUP}, det)


def cyl_re200_time(stat):
    t = float(stat.get("final_physical_time", 0.0) or 0.0)
    step = int(stat.get("final_step", 0) or 0)
    if t < CYL_RE200_T_FAIL:
        st = "fail"
        det = f"only reached t={t} (target {CYL_RE200_T_TARGET}); incomplete transient"
    elif t < CYL_RE200_T_TARGET:
        st = "warn"
        det = f"reached t={t} < production target {CYL_RE200_T_TARGET}"
    else:
        st = "pass"
        det = f"reached t={t} (>= target {CYL_RE200_T_TARGET})"
    return _check(st, {"final_physical_time": t, "final_step": step,
                       "target_final_time": CYL_RE200_T_TARGET},
                  {"target_time": CYL_RE200_T_TARGET, "fail_below": CYL_RE200_T_FAIL}, det)


def cyl_re200_inner(meta):
    frac = float(meta.get("inner_target_converged_fraction", 0.0) or 0.0)
    misses = int(meta.get("inner_target_misses", 0) or 0)
    omin = int(meta.get("observed_min_inner_iterations", 0) or 0)
    omax = int(meta.get("observed_max_inner_iterations", 0) or 0)
    ratio = float(meta.get("last_inner_residual_ratio", 0.0) or 0.0)
    if frac <= INNER_CONV_FAIL:
        st = "fail"
    elif frac < INNER_CONV_WARN:
        st = "warn"
    else:
        st = "pass"
    return _check(st, {"inner_target_converged_fraction": frac,
                       "inner_target_misses": misses,
                       "observed_min_inner": omin, "observed_max_inner": omax,
                       "last_inner_residual_ratio": ratio},
                  {"converged_fraction_warn": INNER_CONV_WARN},
                  f"inner convergence fraction={frac:.3g}, misses={misses}, "
                  f"observed inner iters [{omin},{omax}]")


def wall_velocity(surf, tag_substr):
    if surf is None:
        return _check("skip", None, None, "surface.csv missing")
    tags = surf.get("tag", [])
    mask = [tag_substr in (t or "") for t in tags]
    if not any(mask):
        return _check("skip", None, None, f"no '{tag_substr}' wall rows")
    idx = np.where(mask)[0]
    u = surf.get("u", np.array([])); v = surf.get("v", np.array([]))
    uu = np.abs(u[idx]) if u.size else np.array([0.0])
    vv = np.abs(v[idx]) if v.size else np.array([0.0])
    maxu = float(np.nanmax(uu)) if uu.size else 0.0
    maxv = float(np.nanmax(vv)) if vv.size else 0.0
    maxsp = max(maxu, maxv)
    return _check("pass" if maxsp < NOSLIP_VEL_MAX else "fail",
                  {"n_rows": int(idx.size), "max_abs_u": maxu, "max_abs_v": maxv, "max_speed": maxsp},
                  {"wall_speed_lt": NOSLIP_VEL_MAX},
                  f"max wall speed={maxsp:.6g} over {idx.size} '{tag_substr}' rows")


def wall_skin_friction(surf, viscous):
    if surf is None or not viscous:
        return _check("skip", None, None, "not a viscous no-slip case")
    tags = surf.get("tag", [])
    mask = ["no_slip" in (t or "") for t in tags]
    if not any(mask):
        return _check("skip", None, None, "no no-slip rows")
    idx = np.where(mask)[0]
    cf = surf.get("cf", np.array([]))
    cfa = np.abs(cf[idx]) if cf.size else np.array([0.0])
    mcf = float(np.nanmax(cfa)) if cfa.size else 0.0
    if mcf > NOSLIP_CF_MIN:
        st = "pass"
    else:
        st = "warn"
    return _check(st, {"n_rows": int(idx.size), "max_abs_cf": mcf,
                       "mean_abs_cf": float(np.nanmean(cfa)) if cfa.size else 0.0},
                  {"cf_min": NOSLIP_CF_MIN},
                  f"max|cf|={mcf:.6g} (skin-friction evidence for viscous wall)")


def slip_wall_normal(surf):
    if surf is None:
        return _check("skip", None, None, "surface.csv missing")
    tags = surf.get("tag", [])
    mask = [(t or "") == "slip_wall" or "slip_wall" in (t or "") for t in tags]
    if not any(mask):
        return _check("skip", None, None, "no slip_wall rows")
    idx = np.where(mask)[0]
    u = surf.get("u", np.array([])); v = surf.get("v", np.array([]))
    nx = surf.get("nx", np.array([])); ny = surf.get("ny", np.array([]))
    if u.size and nx.size:
        vn = u[idx] * nx[idx] + v[idx] * ny[idx]
        vt = np.sqrt(np.maximum(0.0, (u[idx] ** 2 + v[idx] ** 2) - vn ** 2))
        maxvn = float(np.nanmax(np.abs(vn))) if vn.size else 0.0
        maxvt = float(np.nanmax(vt)) if vt.size else 0.0
    else:
        maxvn = 0.0; maxvt = 0.0
    st = "pass" if maxvn < SLIP_VN_MAX else "fail"
    return _check(st, {"n_rows": int(idx.size), "max_abs_vn": maxvn, "max_abs_vt": maxvt},
                  {"normal_velocity_lt": SLIP_VN_MAX},
                  f"max|v_n|={maxvn:.6g} (slip wall), max|v_t|={maxvt:.6g}")


def viscous_force_negligible(forces):
    if forces is None:
        return _check("skip", None, None, "forces.csv missing")
    vd = forces.get("viscous_drag", np.array([0.0]))
    vl = forces.get("viscous_lift", np.array([0.0]))
    mvd = float(np.nanmax(np.abs(vd))) if vd.size else 0.0
    mvl = float(np.nanmax(np.abs(vl))) if vl.size else 0.0
    m = max(mvd, mvl)
    st = "pass" if m < VISCOUS_FORCE_MAX else "fail"
    return _check(st, {"max_abs_viscous_drag": mvd, "max_abs_viscous_lift": mvl},
                  {"viscous_force_lt": VISCOUS_FORCE_MAX},
                  f"max|viscous drag|={mvd:.6g}, max|viscous lift|={mvl:.6g}")


def figures_present(case_id, fig_manifest):
    if fig_manifest is None:
        return _check("skip", None, None, "figure_manifest.csv not generated yet")
    rows = [r for r in fig_manifest if r.get("case_id") == case_id]
    if not rows:
        return _check("skip", {"n_figures": 0}, {"expected": ["mach", "pressure"]},
                      "no figures mapped to this case yet (plotting is a separate step)")
    vars_ = {r.get("variable", "").lower() for r in rows}
    has_mach = any("mach" in v for v in vars_)
    has_pres = any("pressure" in v and "coefficient" not in v for v in vars_)
    if has_mach and has_pres:
        st = "pass"
    else:
        st = "warn"
    det = f"mach={'yes' if has_mach else 'no'}, pressure={'yes' if has_pres else 'no'} (of {len(rows)} figs)"
    return _check(st, {"n_figures": len(rows), "has_mach": has_mach, "has_pressure": has_pres},
                  {"required": ["mach", "pressure"]}, det)


def evaluate_case(rdir, fig_manifest):
    loaded = C.load_result(rdir)
    if not loaded:
        return {"case_id": os.path.basename(rdir), "result_dir": rdir,
                "overall": "fail", "recommended_status": "failed",
                "checks": {"metadata": _check("fail", None, None,
                            "metadata.json/run_status.json missing or unreadable")}}
    meta, stat = loaded
    cid = meta.get("case_id", os.path.basename(rdir))
    kind = C.case_kind(cid)
    viscous = meta.get("viscous_flux", "disabled") not in ("disabled", "", None)

    field = C.read_vtu_cell_data(os.path.join(rdir, "field_final.vtu"))
    surf = C.load_surface(os.path.join(rdir, "surface.csv"))
    forces = C.load_forces(os.path.join(rdir, "forces.csv"))
    final_row = C.final_force_row(os.path.join(rdir, "forces.csv"))

    checks = {}
    checks["field_positive_density_pressure"] = field_positivity(field)
    checks["field_finite"] = field_finite(field)
    checks["field_nontrivial"] = field_nontrivial(field)
    checks["surface_cp_variation"] = surface_cp_variation(surf)
    checks["residual_terminal_trend"] = residual_terminal_trend(
        os.path.join(rdir, "residuals.csv"))

    is_naca = kind["family"] == "naca"
    is_cyl = kind["family"] == "cylinder"
    if is_naca:
        checks["naca_zero_aoa_lift"] = naca_lift(forces, final_row)
        checks["naca_nontrivial_solution"] = naca_nontrivial(surf, field)
    re_val = kind.get("reynolds")
    if is_cyl and re_val == 20:
        checks["cylinder_laminar_positive_drag"] = cyl_re20_drag(forces)
    if is_cyl and (re_val == 200 or "re200" in cid):
        checks["cylinder_re200_unsteady_lift"] = cyl_re200_lift(forces)
        checks["cylinder_re200_production_extent"] = cyl_re200_time(stat)
        checks["cylinder_re200_inner_convergence"] = cyl_re200_inner(meta)

    # wall-condition checks (driven by tags actually present in surface.csv)
    if surf is not None:
        tags = set(surf.get("tag", []))
        if any("no_slip" in t for t in tags):
            checks["no_slip_wall_velocity"] = wall_velocity(surf, "no_slip")
            if viscous:
                checks["no_slip_skin_friction"] = wall_skin_friction(surf, viscous)
        if any("slip_wall" in t for t in tags):
            checks["slip_wall_normal_velocity"] = slip_wall_normal(surf)
            if not viscous:
                checks["inviscid_viscous_force_negligible"] = viscous_force_negligible(forces)

    checks["mach_pressure_figures_present"] = figures_present(cid, fig_manifest)

    statuses = [c["status"] for c in checks.values()]
    if "fail" in statuses:
        overall = "fail"
    elif "warn" in statuses:
        overall = "warn"
    else:
        overall = "pass"

    meta_status = meta.get("convergence_status", stat.get("convergence_status", "unknown"))
    if overall == "fail":
        recommended = "failed"
    elif meta_status in ("failed",):
        recommended = "failed"
    else:
        recommended = meta_status

    return {
        "case_id": cid,
        "result_dir": os.path.relpath(rdir, C.SOLVER_ROOT),
        "family": kind["family"], "mode": kind["mode"],
        "mach": kind.get("mach"), "reynolds": kind.get("reynolds"),
        "metadata_status": meta_status,
        "overall": overall,
        "recommended_status": recommended,
        "checks": checks,
    }


def load_fig_manifest(report_dir):
    p = os.path.join(report_dir, "figure_manifest.csv")
    if not os.path.isfile(p):
        return None
    with open(p, newline="") as f:
        return list(csv.DictReader(f))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result_dirs", nargs="*", help="result directories (one per case)")
    ap.add_argument("--auto", action="store_true",
                    help="auto-discover the best complete dir per case_id")
    ap.add_argument("--results-dir", default=C.RESULTS_DIR)
    ap.add_argument("--report-dir", default=C.REPORT_DIR)
    args = ap.parse_args(argv)

    os.makedirs(args.report_dir, exist_ok=True)
    if args.auto:
        dirs = list(C.best_dir_per_case(args.results_dir).values())
    else:
        dirs = [os.path.abspath(d) for d in args.result_dirs]
    if not dirs:
        ap.error("no result directories given (pass dirs or --auto)")

    seen = set(); clean = []
    for d in dirs:
        d = os.path.abspath(d)
        if d not in seen:
            seen.add(d); clean.append(d)

    fig_manifest = load_fig_manifest(args.report_dir)
    cases = [evaluate_case(d, fig_manifest) for d in clean]

    summary = {
        "n_cases": len(cases),
        "pass": sum(1 for c in cases if c["overall"] == "pass"),
        "warn": sum(1 for c in cases if c["overall"] == "warn"),
        "fail": sum(1 for c in cases if c["overall"] == "fail"),
    }
    out = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "solver_root": C.SOLVER_ROOT,
        "thresholds": THRESHOLDS,
        "summary": summary,
        "cases": cases,
    }
    out_path = os.path.join(args.report_dir, "sanity_checks.json")
    with open(out_path, "w") as f:
        json.dump(_sanitize(out), f, indent=2, allow_nan=False)
    print(f"wrote {out_path}")
    print(f"  summary: {summary}")
    for c in cases:
        print(f"  {c['case_id']}: overall={c['overall']} recommended={c['recommended_status']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

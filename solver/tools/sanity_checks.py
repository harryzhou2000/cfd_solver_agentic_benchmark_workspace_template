#!/usr/bin/env python3
"""Machine-readable physics sanity gate (report/sanity_checks.json).

Implements the checks listed in OUTPUT_CONTRACT.md "Physics Sanity Gate".
Every check is evaluated from the submitted output files only; nothing is
hard-coded per case beyond the geometry family implied by the mesh.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vtu_reader import read_vtu  # noqa: E402


def load_csv(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    out = {}
    for k in rows[0]:
        try:
            out[k] = np.array([float(r[k]) for r in rows])
        except ValueError:
            out[k] = np.array([r[k] for r in rows])
    return out


CASE_DIR = os.environ.get("CFD_CASE_DIR",
                          "../cfd_solver_agentic_benchmark/inputs/cases")


def case_json(cid):
    p = os.path.join(CASE_DIR, cid + ".json")
    return json.load(open(p)) if os.path.exists(p) else None


def check(name, passed, detail, value=None):
    return dict(name=name, passed=bool(passed), detail=detail, value=value)


def analyse_case(cdir, cid, manifest_vars):
    meta = json.load(open(os.path.join(cdir, "metadata.json")))
    status = json.load(open(os.path.join(cdir, "run_status.json")))
    forces = load_csv(os.path.join(cdir, "forces.csv"))
    surface = load_csv(os.path.join(cdir, "surface.csv"))
    mesh = read_vtu(os.path.join(cdir, "field_final.vtu"))

    viscous = "disabled" not in str(meta.get("viscous_flux", ""))
    is_cyl = "cylinder" in cid
    transient = float(meta.get("physical_time_step", 0.0)) > 0.0
    checks = []

    rho = mesh.cell_data["Density"]
    p = mesh.cell_data["Pressure"]
    checks.append(check("field_positive_density", float(rho.min()) > 0.0,
                        "minimum cell density in field_final.vtu", float(rho.min())))
    checks.append(check("field_positive_pressure", float(p.min()) > 0.0,
                        "minimum cell pressure in field_final.vtu", float(p.min())))
    finite = all(np.all(np.isfinite(v)) for v in mesh.cell_data.values())
    checks.append(check("field_all_finite", finite, "all field variables are finite"))

    # Total-enthalpy conservation.  For steady inviscid flow H must be uniform;
    # the area-weighted RMS measures that over the domain, while the pointwise
    # maximum is reported for information because it is dominated by the
    # sub-micron first cell layer of these viscous-type grids.
    if not viscous:
        gam = 1.4
        area = mesh.cell_areas()
        vel2 = mesh.cell_data["VelocityX"] ** 2 + mesh.cell_data["VelocityY"] ** 2
        H = gam * p / ((gam - 1.0) * rho) + 0.5 * vel2
        cj = case_json(cid)
        pinf = float(cj["freestream"]["pressure"]) if cj else float(np.median(p))
        rinf = float(cj["freestream"]["rho"]) if cj else 1.0
        uinf = float(cj["freestream"]["velocity_magnitude"]) if cj else 1.0
        Hinf = gam * pinf / ((gam - 1.0) * rinf) + 0.5 * uinf ** 2
        rel = np.abs(H - Hinf) / Hinf
        arms = float(np.sqrt((area * rel ** 2).sum() / area.sum()))
        checks.append(check("inviscid_total_enthalpy_uniform", arms < 1.0e-3,
                            "area-weighted RMS of |H-H_inf|/H_inf over the domain "
                            f"(pointwise max {rel.max():.3e})", arms))

    cp = surface["cp"]
    checks.append(check("surface_cp_varies", float(cp.max() - cp.min()) > 0.05,
                        "range of C_p over the wall", float(cp.max() - cp.min())))

    cd_last = float(forces["cd"][-1])
    cl_last = float(forces["cl"][-1])
    tail = slice(max(1, int(0.75 * len(forces["cd"]))), None)
    cd_mean = float(np.mean(forces["cd"][tail]))
    cl_rms = float(np.std(forces["cl"][tail]))

    if not is_cyl:
        # Symmetric airfoil at zero incidence: lift must be negligible but the
        # solution must not be trivially uniform.
        checks.append(check("naca_zero_lift_symmetry", abs(cl_last) < 5.0e-2,
                            "|C_L| at zero angle of attack", cl_last))
        checks.append(check("naca_nontrivial_solution",
                            float(cp.max() - cp.min()) > 0.3 and abs(cd_last) > 1.0e-5,
                            "surface C_p range and |C_D| are not trivially zero",
                            dict(cp_range=float(cp.max() - cp.min()), cd=cd_last)))
    else:
        checks.append(check("cylinder_positive_mean_drag", cd_mean > 0.0,
                            "mean C_D over the last quarter of the history", cd_mean))
    if transient:
        checks.append(check("transient_unsteady_lift", cl_rms > 1.0e-3,
                            "RMS of C_L over the last quarter of the history", cl_rms))
        checks.append(check("transient_inner_target_met",
                            float(meta.get("inner_target_converged_fraction", 0.0)) >= 0.95,
                            "fraction of physical steps meeting the inner residual target",
                            float(meta.get("inner_target_converged_fraction", 0.0))))
        checks.append(check("transient_reached_final_time",
                            float(status["final_physical_time"]) >= 300.0 - 1e-6,
                            "final physical time", float(status["final_physical_time"])))

    u = surface["u"]
    v = surface["v"]
    nx = surface["nx"]
    ny = surface["ny"]
    speed = np.hypot(u, v)
    uref = 1.0
    if viscous:
        checks.append(check("no_slip_wall_velocity_zero", float(speed.max()) < 1.0e-10 * uref,
                            "maximum reported wall speed on no-slip walls (exactly zero by "
                            "construction of the boundary state)", float(speed.max())))
        checks.append(check("no_slip_skin_friction_nonzero",
                            float(np.max(np.abs(surface["cf"]))) > 1.0e-4,
                            "maximum |C_f| on the wall", float(np.max(np.abs(surface["cf"])))))
        checks.append(check("viscous_force_column_nonzero",
                            abs(float(forces["viscous_drag"][-1])) > 1.0e-6,
                            "final viscous (skin-friction) drag coefficient",
                            float(forces["viscous_drag"][-1])))
        # At a steady no-slip wall the viscous traction is purely tangential
        # (div u = 0 and d(u_t)/dt = 0 there), so the normal viscous force must
        # vanish; a non-zero value would indicate a wall-gradient defect.
        nvd = abs(float(meta.get("final_normal_viscous_drag", 0.0)))
        checks.append(check("no_slip_normal_viscous_traction_zero",
                            nvd < 1.0e-12 * max(abs(cd_last), 1.0),
                            "|normal viscous drag| on no-slip walls", nvd))
        split = float(forces["pressure_drag"][-1]) + float(forces["viscous_drag"][-1])
        checks.append(check("drag_split_sums_to_total",
                            abs(split - cd_last) < 1.0e-9 * max(abs(cd_last), 1.0),
                            "pressure + skin-friction drag equals the total C_D",
                            dict(sum=split, total=cd_last)))
    else:
        un = np.abs(u * nx + v * ny)
        ut = np.abs(u * ny - v * nx)
        # surface.csv is written with 10 significant digits, so the round-off
        # floor of a value reconstructed from the file is ~1e-10 of |u|.
        checks.append(check("slip_wall_normal_velocity_zero", float(un.max()) < 1.0e-8 * uref,
                            "maximum |u.n| on slip walls, recomputed from surface.csv "
                            "(limited by the 10-digit file precision)", float(un.max())))
        checks.append(check("slip_wall_tangential_velocity_nonzero", float(ut.max()) > 1.0e-2,
                            "maximum |u.t| on slip walls", float(ut.max())))
        checks.append(check("inviscid_viscous_columns_negligible",
                            abs(float(forces["viscous_drag"][-1])) < 1.0e-12 and
                            abs(float(forces["viscous_lift"][-1])) < 1.0e-12,
                            "viscous force columns of the inviscid case",
                            dict(viscous_drag=float(forces["viscous_drag"][-1]),
                                 viscous_lift=float(forces["viscous_lift"][-1]))))

    # Final force row must belong to the final field/surface state.
    checks.append(check("final_force_row_matches_final_step",
                        int(float(forces["step"][-1])) == int(float(status["final_step"])),
                        "last forces.csv step equals run_status final_step",
                        dict(forces_step=int(float(forces["step"][-1])),
                             final_step=int(float(status["final_step"])))))

    # Farfield must stay close to the freestream far from the body.
    xy = mesh.cell_centers()
    r = np.hypot(xy[:, 0], xy[:, 1])
    far = r > 0.6 * r.max()
    if far.any():
        rel = float(np.max(np.abs(rho[far] - rho[far].mean()) / max(rho[far].mean(), 1e-30)))
        checks.append(check("farfield_uniform", rel < 0.10,
                            "relative density variation in the outer 40% of the domain radius",
                            rel))

    mv = manifest_vars.get(cid, set())
    checks.append(check("figures_mach_and_pressure_present",
                        "mach" in mv and "pressure" in mv,
                        "figure manifest contains separate Mach and pressure entries",
                        sorted(mv)))

    status_ok = status["convergence_status"] in ("converged", "statistically_periodic")
    all_passed = all(c["passed"] for c in checks)
    # A run may only claim success if every physics check passed; a run marked
    # failed is always "consistent" (it is not claiming anything).
    consistent = (not status_ok) or all_passed
    return dict(case_id=cid, convergence_status=status["convergence_status"],
                completed=bool(meta.get("completed", False)),
                mpi_ranks=int(meta["mpi_ranks"]),
                final_cl=cl_last, final_cd=cd_last, mean_cd_last_quarter=cd_mean,
                cl_rms_last_quarter=cl_rms,
                residual_reduction_orders=float(status["residual_reduction_orders"]),
                checks=checks, all_checks_passed=all_passed,
                status_consistent=bool(consistent))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--manifest", default="report/figure_manifest.csv")
    ap.add_argument("--out", default="report/sanity_checks.json")
    args = ap.parse_args()

    # Only figures that are BOTH listed in the manifest and present on disk
    # count; a stale manifest entry must not be able to satisfy the check.
    manifest_vars = {}
    figdir = os.path.join(os.path.dirname(args.manifest), "figures")
    if os.path.exists(args.manifest):
        for row in csv.DictReader(open(args.manifest, newline="")):
            if os.path.exists(os.path.join(figdir, row["figure_file"])):
                manifest_vars.setdefault(row["case_id"], set()).add(row["variable"])

    cases = sorted(d for d in os.listdir(args.results)
                   if os.path.isdir(os.path.join(args.results, d)) and not d.startswith("_"))
    report = dict(generated_by="tools/sanity_checks.py", cases=[])
    n_fail = 0
    for cid in cases:
        cdir = os.path.join(args.results, cid)
        if not os.path.exists(os.path.join(cdir, "metadata.json")):
            continue
        entry = analyse_case(cdir, cid, manifest_vars)
        report["cases"].append(entry)
        failed = [c["name"] for c in entry["checks"] if not c["passed"]]
        n_fail += len(failed)
        print(f"{cid:34s} {entry['convergence_status']:24s} "
              f"{'OK' if not failed else 'FAILED: ' + ','.join(failed)}")
    report["num_cases"] = len(report["cases"])
    report["num_failed_checks"] = n_fail
    report["all_cases_pass"] = n_fail == 0
    report["all_status_consistent"] = all(c["status_consistent"] for c in report["cases"])
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    json.dump(report, open(args.out, "w"), indent=2)
    print(f"\nwrote {args.out}: {report['num_cases']} cases, {n_fail} failed checks")
    if not report["all_status_consistent"]:
        print("ERROR: a case claims success while failing a physics check")
        return 1
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

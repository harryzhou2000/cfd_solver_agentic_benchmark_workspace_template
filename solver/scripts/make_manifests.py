#!/usr/bin/env python3
"""Write report/run_manifest.csv, report/figure_manifest.csv and
report/sanity_checks.json from the result directories."""

import csv
import json
import math
import os
import sys


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


def read_csv(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def main():
    results = sys.argv[1] if len(sys.argv) > 1 else "results"
    report = sys.argv[2] if len(sys.argv) > 2 else "report"
    os.makedirs(os.path.join(report, "figures"), exist_ok=True)

    run_rows = []
    for case in CASES:
        rdir = os.path.join(results, case)
        st = os.path.join(rdir, "run_status.json")
        md = os.path.join(rdir, "metadata.json")
        if not (os.path.exists(st) and os.path.exists(md)):
            continue
        s = json.load(open(st))
        m = json.load(open(md))
        run_rows.append({
            "case_id": case,
            "command": f"mpirun -np {s['mpi_ranks']} ./build/cfd_solver solve "
                       f"--case inputs/cases/{case}.json --output results/{case}",
            "mpi_ranks": s["mpi_ranks"],
            "wall_time_seconds": round(s["wall_time_seconds"], 1),
            "final_step": s["final_step"],
            "residual_reduction_orders": round(s["residual_reduction_orders"], 3),
            "convergence_status": s["convergence_status"],
            "notes": s.get("notes", ""),
        })
    with open(os.path.join(report, "run_manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(run_rows[0].keys()))
        w.writeheader()
        w.writerows(run_rows)

    figure_rows = []
    figdir = os.path.join(report, "figures")
    if os.path.isdir(figdir):
        for fn in sorted(os.listdir(figdir)):
            if not fn.endswith(".png"):
                continue
            stem = fn[:-4]
            suffixes = [
                ("_velocity_wake", "velocity_wake", "velocity"),
                ("_pressure_wake", "pressure_wake", "pressure"),
                ("_surface_cp", "surface_cp", "cp"),
                ("_surface_cf", "surface_cf", "cf"),
                ("_mach_near", "mach_near", "mach"),
                ("_pressure_near", "pressure_near", "pressure"),
                ("_mach_full", "mach_full", "mach"),
                ("_pressure_full", "pressure_full", "pressure"),
                ("_velocity_near", "velocity_near", "velocity"),
                ("_residual", "residual", "residual_l2"),
                ("_forces", "forces", "cd,cl"),
            ]
            matched = False
            for suffix, ftype, variable in suffixes:
                if stem.endswith(suffix):
                    case = stem[: -len(suffix)]
                    matched = True
                    break
            if not matched:
                continue
            figure_rows.append({
                "figure_file": fn,
                "case_id": case,
                "figure_type": ftype,
                "variable": variable,
                "source_file": (f"results/{case}/residuals.csv"
                                if ftype == "residual" else
                                f"results/{case}/forces.csv"
                                if ftype == "forces" else
                    f"results/{case}/surface.csv" if ftype == "surface_cp" else
                    f"results/{case}/field_final.vtu"),
                "caption": f"{case}: {variable}",
            })
    with open(os.path.join(report, "figure_manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(figure_rows[0].keys()))
        w.writeheader()
        w.writerows(figure_rows)

    sanity = {"checks": [], "cases": {}}
    for case in CASES:
        rdir = os.path.join(results, case)
        field = os.path.join(rdir, "field_final.vtu")
        forces = os.path.join(rdir, "forces.csv")
        surface = os.path.join(rdir, "surface.csv")
        entry = {}
        if os.path.exists(field):
            txt = open(field).read()
            # crude density/pressure positivity: parse only the scalar arrays
            import re
            def array(name):
                m = re.search(r'Name="%s"[^>]*>\s*(.*?)\s*</DataArray>' % name, txt, re.S)
                if not m:
                    return []
                return [float(x) for x in m.group(1).split()]
            rho = array("density")
            p = array("pressure")
            entry["positive_density"] = bool(rho) and min(rho) > 0
            entry["positive_pressure"] = bool(p) and min(p) > 0
            entry["density_min"] = min(rho) if rho else None
            entry["pressure_min"] = min(p) if p else None
        if os.path.exists(forces):
            frows = read_csv(forces)
            cd = [float(r["cd"]) for r in frows]
            cl = [float(r["cl"]) for r in frows]
            n = len(cd)
            entry["mean_cd"] = sum(cd[-max(1, n // 4):]) / max(1, min(n, n // 4))
            entry["mean_cl"] = sum(cl[-max(1, n // 4):]) / max(1, min(n, n // 4))
            cl_amp = max(abs(min(cl[-500:])), abs(max(cl[-500:]))) if n > 500 else 0.0
            entry["cl_variation_last_500"] = cl_amp
            if "inviscid" in case:
                entry["max_viscous_drag"] = max(abs(float(r["viscous_drag"])) for r in frows)
                entry["max_viscous_lift"] = max(abs(float(r["viscous_lift"])) for r in frows)
        if os.path.exists(surface):
            rows = read_csv(surface)
            cp = [float(r["cp"]) for r in rows]
            entry["cp_range"] = [min(cp), max(cp)]
            entry["cp_varies"] = max(cp) - min(cp) > 1e-6
            entry["max_surface_speed"] = max(
                math.hypot(float(r["u"]), float(r["v"])) for r in rows)
            entry["max_surface_normal_velocity"] = max(
                abs(float(r["u"]) * float(r["nx"]) +
                    float(r["v"]) * float(r["ny"])) for r in rows)
            entry["max_surface_tangential_speed"] = max(
                abs(float(r["u"]) * float(r["ny"]) -
                    float(r["v"]) * float(r["nx"])) for r in rows)
        if case.endswith("re200") and os.path.exists(
                os.path.join(rdir, "metadata.json")):
            entry["inner_target_converged_fraction"] = json.load(
                open(os.path.join(rdir, "metadata.json")))[
                "inner_target_converged_fraction"]
        sanity["cases"][case] = entry

    checks = []
    for case, e in sanity["cases"].items():
        ok = lambda cond, label: f"{case}: PASS {label}" if cond else \
            f"{case}: FAIL {label}"
        checks.append(ok(e.get("positive_density", False), "positive density"))
        checks.append(ok(e.get("positive_pressure", False), "positive pressure"))
        if case.startswith("naca"):
            checks.append(ok(abs(e.get("mean_cl", 0)) <= 0.1,
                             "near-zero mean CL"))
        if case.startswith("cylinder") and not case.endswith("re200"):
            checks.append(ok(e.get("mean_cd", 0) > 0, "positive mean drag"))
        if case.endswith("re200"):
            checks.append(ok(e.get("cl_variation_last_500", 0) >= 1e-4,
                             "unsteady lift variation"))
            checks.append(ok(e.get("inner_target_converged_fraction", 0) >= 0.95,
                             "inner-solve converged-step fraction >= 0.95"))
        checks.append(ok(bool(e.get("cp_varies")), "nontrivial surface Cp"))
        if "laminar" in case:
            checks.append(ok(e.get("max_surface_speed", 1e9) < 1e-6,
                             "no-slip wall speed near zero"))
        if "inviscid" in case:
            checks.append(ok(e.get("max_surface_normal_velocity", 1e9) < 1e-6,
                             "slip-wall normal velocity near zero"))
            checks.append(ok(e.get("max_viscous_drag", 1e9) < 1e-8 and
                             e.get("max_viscous_lift", 1e9) < 1e-8,
                             "negligible inviscid viscous forces"))
    sanity["checks"] = checks
    with open(os.path.join(report, "sanity_checks.json"), "w") as f:
        json.dump(sanity, f, indent=2)
    print("manifests written")


if __name__ == "__main__":
    main()

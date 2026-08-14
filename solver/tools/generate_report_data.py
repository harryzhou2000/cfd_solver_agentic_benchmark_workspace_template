#!/usr/bin/env python3
"""Generate sanity_checks.json, figure_manifest.csv, and run_manifest.csv from results."""
import sys, os, csv, json
import numpy as np

RESULTS_DIR = sys.argv[1] if len(sys.argv) > 1 else "solver/results"
REPORT_DIR = sys.argv[2] if len(sys.argv) > 2 else "solver/report"
FIGURES_DIR = os.path.join(REPORT_DIR, "figures")

CASE_IDS = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000", "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200"
]

def read_csv(path):
    rows = []
    if not os.path.exists(path): return rows
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    return rows

def read_json(path):
    if not os.path.exists(path): return {}
    with open(path) as f:
        return json.load(f)

def generate_sanity_checks():
    checks = {"cases": {}}
    for cid in CASE_IDS:
        outdir = os.path.join(RESULTS_DIR, cid)
        meta = read_json(os.path.join(outdir, "metadata.json"))
        if not meta:
            checks["cases"][cid] = {"status": "missing"}
            continue
        status = meta.get("convergence_status", "failed")
        c = {"convergence_status": status}
        field_path = os.path.join(outdir, "field_final.vtu")
        c["field_file_exists"] = os.path.exists(field_path)
        forces = read_csv(os.path.join(outdir, "forces.csv"))
        if forces:
            last = forces[-1]
            cd = float(last["cd"])
            cl = float(last["cl"])
            c["final_cd"] = cd
            c["final_cl"] = cl
            c["positive_drag"] = cd > -0.5
            if "re200" in cid:
                cls = [float(r["cl"]) for r in forces[-100:]] if len(forces) > 100 else [float(r["cl"]) for r in forces]
                c["nonzero_lift_variation"] = (max(cls) - min(cls)) > 0.01
            if "inviscid" in cid:
                c["viscous_drag_near_zero"] = abs(float(last["viscous_drag"])) < 1e-6
        surface = read_csv(os.path.join(outdir, "surface.csv"))
        if surface:
            cps = [float(r["cp"]) for r in surface]
            c["cp_varies_along_wall"] = (max(cps) - min(cps)) > 0.01
            us = [abs(float(r["u"])) for r in surface]
            vs = [abs(float(r["v"])) for r in surface]
            c["wall_velocity_near_zero"] = max(us) < 0.1 and max(vs) < 0.1
        c["all_checks_passed"] = all(v for k, v in c.items() if isinstance(v, bool))
        checks["cases"][cid] = c
    checks["all_cases_completed"] = all(
        checks["cases"].get(cid, {}).get("convergence_status") in ("converged", "statistically_periodic")
        for cid in CASE_IDS
    )
    with open(os.path.join(REPORT_DIR, "sanity_checks.json"), "w") as f:
        json.dump(checks, f, indent=2)
    print("Generated sanity_checks.json")

def generate_figure_manifest():
    entries = []
    for cid in CASE_IDS:
        outdir = os.path.join(RESULTS_DIR, cid)
        if not os.path.exists(outdir): continue
        for fig, vtype, var, src, cap in [
            (f"{cid}_residual.png", "line", "residual_l2", f"{cid}/residuals.csv", f"Residual history for {cid}"),
            (f"{cid}_forces.png", "line", "cd_cl", f"{cid}/forces.csv", f"Force coefficient history for {cid}"),
            (f"{cid}_cp.png", "scatter", "cp", f"{cid}/surface.csv", f"Surface pressure coefficient for {cid}"),
            (f"{cid}_mach.png", "contour", "mach", f"{cid}/field_final.vtu", f"Mach number contour for {cid}"),
            (f"{cid}_pressure.png", "contour", "pressure", f"{cid}/field_final.vtu", f"Pressure contour for {cid}"),
        ]:
            if os.path.exists(os.path.join(FIGURES_DIR, fig)):
                entries.append({"figure_file": fig, "case_id": cid, "figure_type": vtype,
                               "variable": var, "source_file": src, "caption": cap})
        if "re200" in cid:
            for fig, var, cap in [
                (f"{cid}_velocity_u.png", "velocity", f"Velocity wake visualization for {cid}"),
                (f"{cid}_velocity_magnitude.png", "velocity_magnitude", f"Velocity magnitude wake for {cid}"),
            ]:
                if os.path.exists(os.path.join(FIGURES_DIR, fig)):
                    entries.append({"figure_file": fig, "case_id": cid, "figure_type": "contour",
                                   "variable": var, "source_file": f"{cid}/field_final.vtu", "caption": cap})
    with open(os.path.join(REPORT_DIR, "figure_manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file","case_id","figure_type","variable","source_file","caption"])
        w.writeheader()
        w.writerows(entries)
    print(f"Generated figure_manifest.csv with {len(entries)} entries")

def generate_run_manifest():
    entries = []
    for cid in CASE_IDS:
        outdir = os.path.join(RESULTS_DIR, cid)
        meta = read_json(os.path.join(outdir, "metadata.json"))
        status = read_json(os.path.join(outdir, "run_status.json"))
        if not meta:
            entries.append({"case_id": cid, "mpi_ranks": "", "steps": "", "physical_time": "",
                           "residual_reduction": "", "wall_time": "", "status": "not_run"})
            continue
        entries.append({
            "case_id": cid, "mpi_ranks": meta.get("mpi_ranks", ""),
            "steps": status.get("final_step", ""), "physical_time": status.get("final_physical_time", ""),
            "residual_reduction": status.get("residual_reduction_orders", ""),
            "wall_time": f"{status.get('wall_time_seconds', 0):.1f}",
            "status": status.get("convergence_status", "")
        })
    with open(os.path.join(REPORT_DIR, "run_manifest.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["case_id","mpi_ranks","steps","physical_time","residual_reduction","wall_time","status"])
        w.writeheader()
        w.writerows(entries)
    print("Generated run_manifest.csv")

if __name__ == "__main__":
    generate_sanity_checks()
    generate_figure_manifest()
    generate_run_manifest()

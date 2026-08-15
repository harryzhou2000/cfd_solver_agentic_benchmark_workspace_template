#!/usr/bin/env python3
"""Build figures, figure_manifest, run_manifest, and sanity_checks for the report."""
import os, sys, json, csv, subprocess, glob

SOLVER = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(SOLVER, "results")
REPORT = os.path.join(SOLVER, "report")
FIGS = os.path.join(REPORT, "figures")
PY = os.path.join(SOLVER, ".venv", "bin", "python")

CASES = ["naca0012_m015_inviscid","naca0012_m080_inviscid","naca0012_m200_inviscid",
          "naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000","naca0012_m200_laminar_re5000",
          "cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]

def read_csv(path):
    if not os.path.exists(path): return []
    with open(path) as f:
        return list(csv.DictReader(f))

def last_row(csvpath):
    rows = read_csv(csvpath)
    return rows[-1] if rows else {}

def main():
    os.makedirs(FIGS, exist_ok=True)
    fig_manifest = []
    run_manifest = []
    sanity = {"cases": {}}
    for cid in CASES:
        out = os.path.join(RESULTS, cid)
        if not os.path.isdir(out):
            continue
        # generate figures
        try:
            subprocess.run([PY, os.path.join(SOLVER,"tools","plot_results.py"), out, cid],
                           check=False, timeout=120, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except Exception:
            pass
        # copy figures into report/figures with case prefix
        for f in ["residuals.png","forces.png","surface_cp.png","mach.png","pressure.png","vorticity_wake.png"]:
            src = os.path.join(out, "figures", f)
            if os.path.exists(src):
                dst = os.path.join(FIGS, "%s_%s" % (cid, f))
                try:
                    with open(src,"rb") as a, open(dst,"wb") as b: b.write(a.read())
                except Exception: pass
                var = "mach" if "mach" in f else ("pressure" if "pressure" in f else ("vorticity/velocity" if "vorticity" in f else ("residual" if "residual" in f else ("force" if "force" in f else "cp"))))
                ftype = "contour" if f in ("mach.png","pressure.png","vorticity_wake.png") else "line"
                fig_manifest.append({"figure_file":"%s_%s"%(cid,f),"case_id":cid,"figure_type":ftype,
                    "variable":var,"source_file":f.replace(".png",".csv").replace("vorticity_wake","forces") if f not in ("mach.png","pressure.png") else "field_final.vtu",
                    "caption":"%s: %s" % (cid, var)})
        # run manifest
        meta = {}; stat = {}
        try:
            meta = json.load(open(os.path.join(out,"metadata.json")))
        except Exception: pass
        try:
            stat = json.load(open(os.path.join(out,"run_status.json")))
        except Exception: pass
        fr = last_row(os.path.join(out,"forces.csv"))
        rr = last_row(os.path.join(out,"residuals.csv"))
        run_manifest.append({
            "case_id": cid,
            "command": stat.get("command",""),
            "mpi_ranks": meta.get("mpi_ranks", stat.get("mpi_ranks","")),
            "wall_time_seconds": stat.get("wall_time_seconds",""),
            "final_step": stat.get("final_step",""),
            "convergence_status": stat.get("convergence_status",""),
            "residual_reduction_orders": stat.get("residual_reduction_orders",""),
            "final_cd": fr.get("cd",""),
            "final_cl": fr.get("cl",""),
        })
        # sanity checks
        srows = read_csv(os.path.join(out,"surface.csv"))
        surf_ok = all(float(r.get("pressure",1))>0 and float(r.get("rho",1))>0 for r in srows) if srows else True
        cd = float(fr.get("cd",0)) if fr.get("cd") else 0
        cl = float(fr.get("cl",0)) if fr.get("cl") else 0
        status = stat.get("convergence_status","missing")
        cs = {
            "convergence_status": status,
            "final_cd": cd, "final_cl": cl,
            "surface_positive_rho_p": surf_ok,
            "near_zero_lift_aoa0": (abs(cl) < 0.1) if ("naca" in cid and "m015" in cid) else None,
            "positive_cylinder_drag": (cd > 0) if "cylinder" in cid else None,
        }
        sanity["cases"][cid] = cs
    # write manifests
    with open(os.path.join(REPORT,"figure_manifest.csv"),"w",newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file","case_id","figure_type","variable","source_file","caption"])
        w.writeheader(); w.writerows(fig_manifest)
    with open(os.path.join(REPORT,"run_manifest.csv"),"w",newline="") as f:
        w = csv.DictWriter(f, fieldnames=["case_id","command","mpi_ranks","wall_time_seconds","final_step","convergence_status","residual_reduction_orders","final_cd","final_cl"])
        w.writeheader(); w.writerows(run_manifest)
    json.dump(sanity, open(os.path.join(REPORT,"sanity_checks.json"),"w"), indent=2)
    print("wrote figure_manifest.csv, run_manifest.csv, sanity_checks.json")
    print("figures:", len(fig_manifest), "entries")

if __name__ == "__main__":
    main()

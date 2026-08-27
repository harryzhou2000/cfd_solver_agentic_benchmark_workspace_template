#!/usr/bin/env python3
import json, os, csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.tri as mtri
import xml.etree.ElementTree as ET

RESULTS_DIR = "/workspace/solver/results"
REPORT_DIR = "/workspace/solver/report"
FIGURES_DIR = REPORT_DIR + "/figures"
GIT_HASH = "20c70b26f1ec07cda84c494228ca7052045439f3"
BINARY = "/workspace/solver/build/cfd_solver"
os.makedirs(FIGURES_DIR, exist_ok=True)

COMPLETED = ["naca0012_m015_inviscid","naca0012_m080_inviscid","naca0012_m200_inviscid"]
ALL_CASES = COMPLETED + ["naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000","cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]
PARTIAL = ALL_CASES[3:]
status_log = []

def parse_vtu(path):
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//Piece")
    n_points = int(piece.get("NumberOfPoints"))
    n_cells = int(piece.get("NumberOfCells"))
    data = {}
    for da in piece.find("Points").findall("DataArray"):
        arr = np.fromstring(" ".join(da.text.split()), dtype=float, sep=" ")
        data["points"] = arr.reshape(n_points, 3)
    cells_elem = piece.find("Cells")
    connectivity = offsets = None
    for da in cells_elem.findall("DataArray"):
        nm = da.get("Name")
        raw = " ".join(da.text.split())
        if nm == "connectivity":
            connectivity = np.fromstring(raw, dtype=int, sep=" ")
        elif nm == "offsets":
            offsets = np.fromstring(raw, dtype=int, sep=" ")
    cells_list = []
    prev = 0
    for off in offsets:
        cells_list.append(connectivity[prev:off])
        prev = off
    pts = data["points"][:, :2]
    data["cell_centers"] = np.array([pts[c].mean(axis=0) for c in cells_list])
    cd_elem = piece.find("CellData")
    if cd_elem is not None:
        for da in cd_elem.findall("DataArray"):
            nm = da.get("Name")
            ncomp = int(da.get("NumberOfComponents") or 1)
            arr = np.fromstring(" ".join(da.text.split()), dtype=float, sep=" ")
            data[nm] = arr.reshape(n_cells, ncomp) if ncomp > 1 else arr
    return data

def plot_field(case_id, fd, field_name, title, cmap, filename):
    centers = fd["cell_centers"]
    values = fd[field_name]
    if values.ndim > 1:
        values = np.linalg.norm(values[:, :2], axis=1)
    x, y, z = centers[:,0], centers[:,1], values
    mask = (x > -1.5) & (x < 3.0) & (y > -1.5) & (y < 1.5)
    xm, ym, zm = x[mask], y[mask], z[mask]
    fig, ax = plt.subplots(figsize=(10,6))
    try:
        triang = mtri.Triangulation(xm, ym)
        tri_pts = np.stack([xm[triang.triangles], ym[triang.triangles]], axis=-1)
        max_edge = np.zeros(len(triang.triangles))
        for i in range(3):
            j = (i+1) % 3
            d = np.linalg.norm(tri_pts[:,i]-tri_pts[:,j], axis=-1)
            max_edge = np.maximum(max_edge, d)
        triang.set_mask(max_edge > 0.3)
        vmin, vmax = np.percentile(zm,2), np.percentile(zm,98)
        cf = ax.tricontourf(triang, zm, levels=np.linspace(vmin,vmax,50), cmap=cmap, extend="both")
        plt.colorbar(cf, ax=ax, label=title)
    except Exception as e:
        sc = ax.scatter(xm, ym, c=zm, cmap=cmap, s=1, rasterized=True)
        plt.colorbar(sc, ax=ax, label=title)
    ax.set_aspect("equal")
    ax.set_xlabel("x/c")
    ax.set_ylabel("y/c")
    ax.set_title(title + " - " + case_id.replace("_"," "))
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches="tight")
    plt.close()

def plot_residuals(case_id, res_path, filename):
    steps, residuals = [], []
    with open(res_path) as f:
        for row in csv.DictReader(f):
            steps.append(int(row["step"]))
            residuals.append(float(row["residual_l2"]))
    fig, ax = plt.subplots(figsize=(8,5))
    ax.semilogy(steps, residuals, "b-", lw=1.2)
    ax.set_xlabel("Iteration")
    ax.set_ylabel("L2 Residual")
    ax.set_title("Convergence - " + case_id.replace("_"," "))
    ax.grid(True, alpha=0.4)
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches="tight")
    plt.close()

def plot_forces(case_id, forces_path, filename):
    steps, cl_vals, cd_vals = [], [], []
    with open(forces_path) as f:
        for row in csv.DictReader(f):
            steps.append(int(row["step"]))
            cl_vals.append(float(row["cl"]))
            cd_vals.append(float(row["cd"]))
    fig, (ax1, ax2) = plt.subplots(2,1,figsize=(8,7), sharex=True)
    ax1.plot(steps, cd_vals, "r-", lw=1.2)
    ax1.set_ylabel("Cd")
    ax1.set_title("Force History - " + case_id.replace("_"," "))
    ax1.grid(True, alpha=0.4)
    ax2.plot(steps, cl_vals, "b-", lw=1.2)
    ax2.set_ylabel("Cl")
    ax2.set_xlabel("Iteration")
    ax2.grid(True, alpha=0.4)
    plt.tight_layout()
    plt.savefig(filename, dpi=100, bbox_inches="tight")
    plt.close()

figure_rows = []
sanity_cases = {}

for case_id in COMPLETED:
    cdir = RESULTS_DIR + "/" + case_id
    print("Processing", case_id)
    fd = None
    try:
        fd = parse_vtu(cdir+"/field_final_rank0.vtu")
        print(" cells:", len(fd["cell_centers"]))
    except Exception as e:
        status_log.append("ERROR: VTU " + case_id + ": " + str(e))
    for fname, fvar, ftitle, fcmap in [("mach","mach","Mach Number","RdBu_r"),("pressure","pressure","Pressure","viridis")]:
        figpath = FIGURES_DIR+"/"+case_id+"_"+fname+".png"
        if fd is not None and fvar in fd:
            try:
                plot_field(case_id, fd, fvar, ftitle, fcmap, figpath)
                status_log.append("OK: "+case_id+"_"+fname+".png generated")
                figure_rows.append({
                    "figure_file": "figures/"+case_id+"_"+fname+".png",
                    "case_id": case_id,
                    "figure_type": "field",
                    "variable": fvar,
                    "source_file": "results/"+case_id+"/field_final_rank0.vtu",
                    "caption": ftitle+" for "+case_id
                })
            except Exception as e:
                status_log.append("ERROR: "+fname+" "+case_id+": "+str(e))
    for suffix, func, src in [("residuals",plot_residuals,"residuals.csv"),("forces",plot_forces,"forces.csv")]:
        figpath = FIGURES_DIR+"/"+case_id+"_"+suffix+".png"
        try:
            func(case_id, cdir+"/"+src, figpath)
            status_log.append("OK: "+case_id+"_"+suffix+".png generated")
            figure_rows.append({
                "figure_file": "figures/"+case_id+"_"+suffix+".png",
                "case_id": case_id,
                "figure_type": suffix,
                "variable": suffix,
                "source_file": "results/"+case_id+"/"+src,
                "caption": suffix+" for "+case_id
            })
        except Exception as e:
            status_log.append("ERROR: "+suffix+" "+case_id+": "+str(e))
    try:
        with open(cdir+"/run_status.json") as f:
            rs = json.load(f)
        fr = None
        with open(cdir+"/residuals.csv") as f:
            for row in csv.DictReader(f):
                pass
            fr = float(row["residual_l2"])
        Cd = Cl = None
        with open(cdir+"/forces.csv") as f:
            for row in csv.DictReader(f):
                pass
            Cd = float(row["cd"])
            Cl = float(row["cl"])
        sanity_cases[case_id] = {
            "final_residual": fr,
            "Cd": Cd,
            "Cl": Cl,
            "convergence_orders": rs["residual_reduction_orders"],
            "status": rs["convergence_status"]
        }
    except Exception as e:
        print(" Sanity error:", e)

for case_id in PARTIAL:
    cdir = RESULTS_DIR+"/"+case_id
    rp = cdir+"/residuals.csv"
    fp = cdir+"/forces.csv"
    if not os.path.exists(rp):
        continue
    figpath = FIGURES_DIR+"/"+case_id+"_residuals.png"
    try:
        plot_residuals(case_id, rp, figpath)
        status_log.append("OK: "+case_id+"_residuals.png (partial)")
        figure_rows.append({
            "figure_file": "figures/"+case_id+"_residuals.png",
            "case_id": case_id,
            "figure_type": "convergence",
            "variable": "residual_l2",
            "source_file": "results/"+case_id+"/residuals.csv",
            "caption": "Residuals for "+case_id+" (partial)"
        })
    except Exception as e:
        status_log.append("ERROR: residuals "+case_id+": "+str(e))
    if os.path.exists(fp):
        figpath = FIGURES_DIR+"/"+case_id+"_forces.png"
        try:
            plot_forces(case_id, fp, figpath)
            status_log.append("OK: "+case_id+"_forces.png (partial)")
            figure_rows.append({
                "figure_file": "figures/"+case_id+"_forces.png",
                "case_id": case_id,
                "figure_type": "forces",
                "variable": "Cd/Cl",
                "source_file": "results/"+case_id+"/forces.csv",
                "caption": "Forces for "+case_id+" (partial)"
            })
        except Exception as e:
            status_log.append("ERROR: forces "+case_id+": "+str(e))
    try:
        fr = None
        with open(rp) as f:
            for row in csv.DictReader(f):
                pass
            fr = float(row["residual_l2"])
        Cd = Cl = None
        if os.path.exists(fp):
            with open(fp) as f:
                for row in csv.DictReader(f):
                    pass
                Cd = float(row["cd"])
                Cl = float(row["cl"])
        sanity_cases[case_id] = {
            "final_residual": fr,
            "Cd": Cd,
            "Cl": Cl,
            "convergence_orders": None,
            "status": "running"
        }
    except Exception:
        pass

manifest_rows = []
for case_id in ALL_CASES:
    cdir = RESULTS_DIR+"/"+case_id
    rsp = cdir+"/run_status.json"
    if os.path.exists(rsp):
        with open(rsp) as f:
            rs = json.load(f)
        manifest_rows.append({
            "case_id": case_id,
            "np": rs.get("mpi_ranks",1),
            "final_step": rs.get("final_step","N/A"),
            "convergence_status": rs.get("convergence_status","unknown"),
            "residual_reduction": round(rs.get("residual_reduction_orders",0),2),
            "wall_time_s": round(rs.get("wall_time_seconds",0),1),
            "binary": BINARY,
            "git_commit": GIT_HASH[:12]
        })
    else:
        rp = cdir+"/residuals.csv"
        final_step = "N/A"
        if os.path.exists(rp):
            with open(rp) as f:
                for row in csv.DictReader(f):
                    pass
                try:
                    final_step = int(row["step"])
                except Exception:
                    pass
        manifest_rows.append({
            "case_id": case_id,
            "np": 1,
            "final_step": final_step,
            "convergence_status": "running",
            "residual_reduction": "N/A",
            "wall_time_s": "N/A",
            "binary": BINARY,
            "git_commit": GIT_HASH[:12]
        })

with open(REPORT_DIR+"/run_manifest.csv","w",newline="") as f:
    w = csv.DictWriter(f, fieldnames=["case_id","np","final_step","convergence_status","residual_reduction","wall_time_s","binary","git_commit"])
    w.writeheader()
    w.writerows(manifest_rows)
status_log.append("OK: run_manifest.csv written")

with open(REPORT_DIR+"/sanity_checks.json","w") as f:
    json.dump({"timestamp":"2026-08-27","cases":sanity_cases}, f, indent=2)
status_log.append("OK: sanity_checks.json written")

with open(REPORT_DIR+"/figure_manifest.csv","w",newline="") as f:
    w = csv.DictWriter(f, fieldnames=["figure_file","case_id","figure_type","variable","source_file","caption"])
    w.writeheader()
    w.writerows(figure_rows)
status_log.append("OK: figure_manifest.csv written")

gf = [s for s in status_log if s.startswith("OK:") and ".png" in s]
errs = [s for s in status_log if s.startswith("ERROR:")]
with open(REPORT_DIR+"/report_status.txt","w") as f:
    f.write("=== CFD Benchmark Report Generation Status ===\n")
    f.write("Generated: 2026-08-27\n\n")
    f.write("Successfully generated figures ({}):\n".format(len(gf)))
    for s in gf:
        f.write("  "+s+"\n")
    f.write("\nErrors ({}):\n".format(len(errs)))
    for e in errs:
        f.write("  "+e+"\n")
    f.write("\nrun_manifest.csv: DONE\n")
    f.write("sanity_checks.json: DONE\n")
    f.write("figure_manifest.csv: DONE\n")
    f.write("report.tex: PENDING\n")

print("DONE - {} figures, {} errors".format(len(gf), len(errs)))
for s in status_log:
    print(" ", s)


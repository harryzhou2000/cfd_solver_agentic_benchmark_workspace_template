#!/usr/bin/env python3
"""Generate all report figures from solver output directories."""
import os, sys, csv, glob, json, math
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

FIG_DIR = "/workspace/solver/report/figures"
os.makedirs(FIG_DIR, exist_ok=True)

def read_csv(path):
    rows = []
    with open(path) as f:
        r = csv.DictReader(f)
        for row in r: rows.append(row)
    return rows

def plot_residuals(case, outdir):
    rows = read_csv(os.path.join(outdir, "residuals.csv"))
    if not rows: return
    steps = [int(float(r["step"])) for r in rows]
    l2 = [float(r["residual_l2"]) for r in rows]
    plt.figure(figsize=(6,4))
    plt.semilogy(steps, l2, lw=1.2)
    plt.xlabel("pseudo-time step"); plt.ylabel("residual L2 (RMS)")
    plt.title(f"{case} residual history"); plt.grid(True, which="both", ls=":", alpha=0.5)
    plt.tight_layout(); plt.savefig(f"{FIG_DIR}/{case}_residual.png", dpi=120); plt.close()

def plot_forces(case, outdir):
    rows = read_csv(os.path.join(outdir, "forces.csv"))
    if not rows: return
    t = [float(r["physical_time"]) for r in rows]
    cd = [float(r["cd"]) for r in rows]
    cl = [float(r["cl"]) for r in rows]
    fig, ax = plt.subplots(1,2, figsize=(11,4))
    ax[0].plot(t, cd, lw=1.0); ax[0].set_xlabel("physical time / step"); ax[0].set_ylabel("cd"); ax[0].set_title(f"{case} drag"); ax[0].grid(True, ls=":", alpha=0.5)
    ax[1].plot(t, cl, lw=1.0); ax[1].set_xlabel("physical time / step"); ax[1].set_ylabel("cl"); ax[1].set_title(f"{case} lift"); ax[1].grid(True, ls=":", alpha=0.5)
    plt.tight_layout(); plt.savefig(f"{FIG_DIR}/{case}_forces.png", dpi=120); plt.close()

def plot_surface(case, outdir):
    path = os.path.join(outdir, "surface.csv")
    if not os.path.exists(path): return
    rows = read_csv(path)
    if not rows: return
    x = [float(r["x"]) for r in rows]
    cp = [float(r["cp"]) for r in rows]
    plt.figure(figsize=(7,4))
    plt.plot(x, cp, ".", ms=2)
    plt.xlabel("x"); plt.ylabel("cp"); plt.title(f"{case} surface cp"); plt.grid(True, ls=":", alpha=0.5)
    plt.tight_layout(); plt.savefig(f"{FIG_DIR}/{case}_surface_cp.png", dpi=120); plt.close()

def parse_vtu(path):
    """Minimal ASCII VTU parser: returns verts (N,2), cells (list of polygon vertex idx), cell_data dict."""
    import xml.etree.ElementTree as ET
    tree = ET.parse(path); root = tree.getroot()
    piece = root.find(".//Piece")
    npts = int(piece.get("NumberOfPoints")); ncells = int(piece.get("NumberOfCells"))
    pts_data = None
    for da in piece.find("Points").findall("DataArray"):
        if da.get("Name") is None or da.get("NumberOfComponents")=="3":
            txt = " ".join(da.text.split())
            pts_data = list(map(float, txt.split()))
    pts = np.array(pts_data).reshape(npts,3)[:,:2]
    cells = piece.find("Cells")
    conn = None; offs = None; types = None
    for da in cells.findall("DataArray"):
        nm = da.get("Name")
        txt = " ".join(da.text.split())
        arr = list(map(int, txt.split()))
        if nm == "connectivity": conn = arr
        elif nm == "offsets": offs = arr
        elif nm == "types": types = arr
    # build cells
    cellverts = []
    start = 0
    for o in offs:
        cellverts.append(conn[start:o]); start = o
    # cell data
    cdata = {}
    cd = piece.find("CellData")
    if cd is not None:
        for da in cd.findall("DataArray"):
            nm = da.get("Name")
            txt = " ".join(da.text.split())
            cdata[nm] = np.array(list(map(float, txt.split())))
    return pts, cellverts, cdata

def plot_field(case, outdir, var, vmin=None, vmax=None, xlim=None, ylim=None):
    path = os.path.join(outdir, "field_final.vtu")
    if not os.path.exists(path): return False
    try:
        pts, cellverts, cdata = parse_vtu(path)
    except Exception as e:
        print(f"  VTU parse failed for {case}: {e}"); return False
    if var not in cdata: print(f"  {var} not in field for {case}"); return False
    val = cdata[var]
    # cell centers
    cc = np.array([pts[np.array(cv)].mean(axis=0) for cv in cellverts])
    x = cc[:,0]; y = cc[:,1]
    plt.figure(figsize=(8,5))
    if vmin is None: vmin = np.percentile(val, 2)
    if vmax is None: vmax = np.percentile(val, 98)
    sc = plt.scatter(x, y, c=val, s=1.2, cmap="jet", vmin=vmin, vmax=vmax)
    plt.colorbar(sc, label=var)
    if xlim: plt.xlim(xlim)
    if ylim: plt.ylim(ylim)
    plt.xlabel("x"); plt.ylabel("y"); plt.title(f"{case} {var}")
    plt.gca().set_aspect("equal")
    plt.tight_layout(); plt.savefig(f"{FIG_DIR}/{case}_{var}.png", dpi=120); plt.close()
    return True

def main():
    out_root = "/workspace/solver/results"
    cases = sorted([d for d in os.listdir(out_root) if os.path.isdir(os.path.join(out_root,d)) and not d.startswith("smoke")])
    manifest = []
    for case in cases:
        outdir = os.path.join(out_root, case)
        print(f"plotting {case}...")
        plot_residuals(case, outdir)
        plot_forces(case, outdir)
        plot_surface(case, outdir)
        is_naca = "naca" in case
        is_cyl = "cylinder" in case
        # field plots: mach and pressure
        ok_m = plot_field(case, outdir, "mach")
        ok_p = plot_field(case, outdir, "pressure")
        if is_naca:
            plot_field(case, outdir, "mach", xlim=(-1,2), ylim=(-1,1))
            plot_field(case, outdir, "pressure", xlim=(-1,2), ylim=(-1,1))
        if is_cyl:
            plot_field(case, outdir, "mach", xlim=(-2,5), ylim=(-2,2))
            plot_field(case, outdir, "pressure", xlim=(-2,5), ylim=(-2,2))
        if "re200" in case:
            plot_field(case, outdir, "velocity_u", xlim=(-2,8), ylim=(-2,2))
        for var,fn in [("mach","mach"),("pressure","pressure")]:
            f = f"{case}_{fn}.png"
            if os.path.exists(f"{FIG_DIR}/{f}"):
                manifest.append({"figure_file":f, "case_id":case, "figure_type":"field", "variable":var, "source_file":f"results/{case}/field_final.vtu", "caption":f"{case} {var} field"})
        if os.path.exists(f"{FIG_DIR}/{case}_residual.png"):
            manifest.append({"figure_file":f"{case}_residual.png","case_id":case,"figure_type":"residual","variable":"residual_l2","source_file":f"results/{case}/residuals.csv","caption":f"{case} residual history"})
        if os.path.exists(f"{FIG_DIR}/{case}_forces.png"):
            manifest.append({"figure_file":f"{case}_forces.png","case_id":case,"figure_type":"forces","variable":"cd_cl","source_file":f"results/{case}/forces.csv","caption":f"{case} force history"})
        if os.path.exists(f"{FIG_DIR}/{case}_surface_cp.png"):
            manifest.append({"figure_file":f"{case}_surface_cp.png","case_id":case,"figure_type":"surface","variable":"cp","source_file":f"results/{case}/surface.csv","caption":f"{case} surface pressure coefficient"})
        if "re200" in case and os.path.exists(f"{FIG_DIR}/{case}_velocity_u.png"):
            manifest.append({"figure_file":f"{case}_velocity_u.png","case_id":case,"figure_type":"wake","variable":"velocity_u","source_file":f"results/{case}/field_final.vtu","caption":f"{case} wake velocity (post-transient)"})
    # write manifest
    with open("/workspace/solver/report/figure_manifest.csv","w",newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file","case_id","figure_type","variable","source_file","caption"])
        w.writeheader()
        for m in manifest: w.writerow(m)
    print(f"wrote {len(manifest)} manifest entries")

if __name__ == "__main__":
    main()

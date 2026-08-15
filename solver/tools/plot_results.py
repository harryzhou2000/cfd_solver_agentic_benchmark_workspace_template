#!/usr/bin/env python3
"""Generate all required plots for the CFD benchmark report."""
import sys, os, json, csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

SOLVER_DIR = Path(__file__).parent.parent
RESULTS = SOLVER_DIR / "results"
FIGURES = SOLVER_DIR / "report" / "figures"
FIGURES.mkdir(parents=True, exist_ok=True)

def read_csv(path):
    with open(path) as f: return list(csv.DictReader(f))

def plot_residuals(case_id, rows):
    fig, ax = plt.subplots(figsize=(8, 5))
    steps = [int(float(r['step'])) for r in rows]
    l2 = [float(r['residual_l2']) for r in rows]
    ax.semilogy(steps, l2, 'b-', linewidth=1.2, label='L2 residual')
    ax.set_xlabel('Step'); ax.set_ylabel('Residual (L2)')
    ax.set_title(f'Residual History: {case_id}')
    ax.grid(True, alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(FIGURES / f'{case_id}_residuals.png', dpi=150); plt.close(fig)

def plot_forces(case_id, rows):
    fig, ax = plt.subplots(figsize=(8, 5))
    steps = [int(float(r['step'])) for r in rows]
    cd = [float(r['cd']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    ax.plot(steps, cd, 'r-', linewidth=1.2, label='Cd')
    ax.plot(steps, cl, 'b-', linewidth=1.0, label='Cl')
    ax.set_xlabel('Step'); ax.set_ylabel('Force Coefficient')
    ax.set_title(f'Force History: {case_id}')
    ax.grid(True, alpha=0.3); ax.legend()
    fig.tight_layout(); fig.savefig(FIGURES / f'{case_id}_forces.png', dpi=150); plt.close(fig)

def plot_surface(case_id, rows):
    if not rows: return
    x = [float(r['x']) for r in rows]
    cp = [float(r['cp']) for r in rows]
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.plot(x, cp, 'b.', markersize=3)
    ax.set_xlabel('x'); ax.set_ylabel('Cp')
    ax.set_title(f'Surface Cp: {case_id}')
    ax.grid(True, alpha=0.3)
    fig.tight_layout(); fig.savefig(FIGURES / f'{case_id}_surface_cp.png', dpi=150); plt.close(fig)

def parse_vtk(path):
    with open(path) as f: lines = f.readlines()
    pts, cells, data = [], [], {}
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith('POINTS'):
            n = int(line.split()[1])
            for j in range(n):
                v = lines[i+1+j].split()
                pts.append((float(v[0]), float(v[1])))
            i += 1 + n
        elif line.startswith('CELLS'):
            n = int(line.split()[1])
            for j in range(n):
                v = lines[i+1+j].split()
                cells.append([int(x) for x in v[1:1+int(v[0])]])
            i += 1 + n
        elif line.startswith('CELL_TYPES'):
            n = int(line.split()[1]); i += 1 + n
        elif line.startswith('SCALARS'):
            var = line.split()[1]; i += 2  # skip SCALARS + LOOKUP_TABLE
            vals = []
            while i < len(lines):
                s = lines[i].strip()
                if not s or s.startswith('SCALARS') or s.startswith('CELL') or s.startswith('LOOKUP'): break
                try: vals.append(float(s))
                except: break
                i += 1
            data[var] = np.array(vals)
        else: i += 1
    return np.array(pts), cells, data

def plot_field(case_id, vtk_path):
    if not os.path.exists(vtk_path): return
    pts, cells, data = parse_vtk(vtk_path)
    if len(pts) == 0 or 'mach' not in data: return
    import matplotlib.tri as mtri
    tris = []
    for c in cells:
        if len(c) == 3: tris.append(c)
        elif len(c) == 4: tris.append([c[0],c[1],c[2]]); tris.append([c[0],c[2],c[3]])

    # Re200 wake visualization: vorticity from velocity field
    if 're200' in case_id and 'u' in data and 'v' in data:
        # compute vorticity at cell centroids using simple finite difference
        import matplotlib.tri as mtri
        tris = []
        for c in cells:
            if len(c) == 3: tris.append(c)
            elif len(c) == 4: tris.append([c[0],c[1],c[2]]); tris.append([c[0],c[2],c[3]])
        ncells = min(len(tris), len(data['u']))
        if ncells > 0:
            # compute cell centroid vorticity = dv/dx - du/dy
            # use triangulation-based gradient
            try:
                triang = mtri.Triangulation(pts[:,0], pts[:,1], tris[:ncells])
                # interpolate u,v to a regular grid for gradient computation
                from scipy.interpolate import griddata
                cents = np.array([np.mean([pts[t[0]],pts[t[1]],pts[t[2]]],axis=0) for t in tris[:ncells]])
                u_vals = data['u'][:ncells]
                v_vals = data['v'][:ncells]
                # create fine grid in wake region
                xi = np.linspace(-1, 10, 200)
                yi = np.linspace(-2, 2, 100)
                xi, yi = np.meshgrid(xi, yi)
                ui = griddata(cents, u_vals, (xi, yi), method='linear', fill_value=0)
                vi = griddata(cents, v_vals, (xi, yi), method='linear', fill_value=0)
                vort = np.gradient(vi, xi[0,:], axis=1) - np.gradient(ui, yi[:,0], axis=0)
                vort = np.clip(vort, -5, 5)  # clip to recommended range
                fig, ax = plt.subplots(figsize=(12, 4))
                tc = ax.pcolormesh(xi, yi, vort, cmap='RdBu_r', vmin=-5, vmax=5, shading='auto')
                plt.colorbar(tc, ax=ax, label='vorticity')
                ax.set_xlabel('x'); ax.set_ylabel('y')
                ax.set_title(f'Vorticity (Wake): {case_id}')
                ax.set_aspect('equal')
                fig.tight_layout()
                fig.savefig(FIGURES / f'{case_id}_vorticity.png', dpi=150)
                plt.close(fig)
            except Exception as e:
                print(f"  vorticity plot failed: {e}")

    for var in ['mach', 'pressure']:
        if var not in data: continue
        fig, ax = plt.subplots(figsize=(10, 6))
        vals = data[var]
        # vals is cell data (one per cell), tris is triangle list
        ncells = min(len(vals), len(tris))
        if ncells == 0:
            plt.close(fig); continue
        try:
            triang = mtri.Triangulation(pts[:,0], pts[:,1], tris[:ncells])
            tc = ax.tricontourf(triang, vals[:ncells], levels=50, cmap='jet')
        except Exception as e:
            # fallback: scatter cell centroids
            cents = np.array([np.mean([pts[t[0]],pts[t[1]],pts[t[2]]],axis=0) for t in tris[:ncells]])
            tc = ax.scatter(cents[:,0], cents[:,1], c=vals[:ncells], s=1, cmap='jet')
        plt.colorbar(tc, ax=ax, label=var)
        ax.set_xlabel('x'); ax.set_ylabel('y')
        ax.set_title(f'{var.capitalize()} Field: {case_id}')
        ax.set_aspect('equal')
        fig.tight_layout(); fig.savefig(FIGURES / f'{case_id}_{var}.png', dpi=150); plt.close(fig)

def main():
    manifest = []
    for cd in sorted(RESULTS.iterdir()):
        if not cd.is_dir(): continue
        cid = cd.name
        rp, fp, sp, vp = cd/'residuals.csv', cd/'forces.csv', cd/'surface.csv', cd/'field_final.vtk'
        if rp.exists():
            plot_residuals(cid, read_csv(rp))
            manifest.append({'figure_file':f'{cid}_residuals.png','case_id':cid,'figure_type':'residual_history','variable':'residual_l2','source_file':str(rp),'caption':f'Residual history for {cid}'})
        if fp.exists():
            plot_forces(cid, read_csv(fp))
            manifest.append({'figure_file':f'{cid}_forces.png','case_id':cid,'figure_type':'force_history','variable':'cd_cl','source_file':str(fp),'caption':f'Force history for {cid}'})
        if sp.exists():
            plot_surface(cid, read_csv(sp))
            manifest.append({'figure_file':f'{cid}_surface_cp.png','case_id':cid,'figure_type':'surface_cp','variable':'cp','source_file':str(sp),'caption':f'Surface Cp for {cid}'})
        if vp.exists():
            plot_field(cid, str(vp))
            for var in ['mach','pressure']:
                if (FIGURES/f'{cid}_{var}.png').exists():
                    manifest.append({'figure_file':f'{cid}_{var}.png','case_id':cid,'figure_type':'field_contour','variable':var,'source_file':str(vp),'caption':f'{var.capitalize()} contour for {cid}'})
    mp = SOLVER_DIR/'report'/'figure_manifest.csv'
    with open(mp,'w',newline='') as f:
        w = csv.DictWriter(f, fieldnames=['figure_file','case_id','figure_type','variable','source_file','caption'])
        w.writeheader()
        for e in manifest: w.writerow(e)
    print(f"Generated {len(manifest)} figures")

if __name__ == '__main__':
    main()

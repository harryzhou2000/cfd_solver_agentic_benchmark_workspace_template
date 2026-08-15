#!/usr/bin/env python3
"""Generate publication-style figures for a CFD case output directory."""
import sys, os, csv, re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.tr import Triangulation

def read_csv(path):
    rows = []
    with open(path) as f:
        for row in csv.DictReader(f):
            rows.append(row)
    return rows

def read_vtu_ascii(path):
    txt = open(path).read()
    def block(name, comp=1):
        m = re.search(r'<DataArray type="[^"]*" Name="%s"[^>]*>(.*?)</DataArray>' % name, txt, re.S)
        if not m:
            return None
        vals = np.fromstring(m.group(1).strip(), sep=' ')
        if comp > 1:
            vals = vals.reshape(-1, comp)
        return vals
    pts = block("Points", comp=3)
    conn = block("connectivity")
    offs = block("offsets")
    if pts is None or conn is None:
        raise RuntimeError("VTU parse failed")
    olist = offs.astype(int)
    clist = conn.astype(int)
    cells = []
    start = 0
    for o in olist:
        cells.append(clist[start:o])
        start = o
    centers = np.array([pts[c, :2].mean(axis=0) for c in cells])
    data = {}
    for nm in ["density", "pressure", "mach_number", "temperature", "rank", "global_id"]:
        v = block(nm)
        if v is not None:
            data[nm] = v
    vel = block("velocity", comp=3)
    if vel is not None:
        data["speed"] = np.sqrt(vel[:, 0] ** 2 + vel[:, 1] ** 2)
    return pts, cells, centers, data

def plot_residuals(out, cid):
    rows = read_csv(os.path.join(out, "residuals.csv"))
    if not rows:
        return
    st = np.array([float(r["step"]) for r in rows])
    l2 = np.array([float(r["residual_l2"]) for r in rows])
    li = np.array([float(r["residual_linf"]) for r in rows])
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.semilogy(st, l2, 'b-', lw=1.2, label='L2 residual')
    ax.semilogy(st, li, 'r-', lw=0.8, alpha=0.6, label='Linf')
    ax.set_xlabel('pseudo-time step')
    ax.set_ylabel('residual (nondim)')
    ax.set_title('%s: residual history' % cid)
    ax.grid(True, which='both', ls=':', alpha=0.4)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, 'figures', 'residuals.png'), dpi=130)
    plt.close(fig)

def plot_forces(out, cid):
    rows = read_csv(os.path.join(out, "forces.csv"))
    if not rows:
        return
    st = np.array([float(r["step"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.plot(st, cd, 'b-', lw=1.2, label='cd')
    ax.plot(st, cl, 'r-', lw=1.2, label='cl')
    ax.set_xlabel('step')
    ax.set_ylabel('force coefficient')
    ax.grid(True, ls=':', alpha=0.4)
    ax.set_title('%s: force history' % cid)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out, 'figures', 'forces.png'), dpi=130)
    plt.close(fig)

def plot_surface(out, cid):
    rows = read_csv(os.path.join(out, "surface.csv"))
    if not rows:
        return
    x = np.array([float(r["x"]) for r in rows])
    cp = np.array([float(r["cp"]) for r in rows])
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.plot(x, cp, 'b.-', ms=3, lw=1.0)
    ax.invert_yaxis()
    ax.set_xlabel('x')
    ax.set_ylabel('cp')
    ax.grid(True, ls=':', alpha=0.4)
    ax.set_title('%s: surface pressure coefficient' % cid)
    fig.tight_layout()
    fig.savefig(os.path.join(out, 'figures', 'surface_cp.png'), dpi=130)
    plt.close(fig)

def plot_field(out, cid, var, fname, title, clip=None, zoom=None):
    fpath = os.path.join(out, "field_final.vtu")
    if not os.path.exists(fpath):
        return None
    pts, cells, centers, data = read_vtu_ascii(fpath)
    if var not in data:
        return None
    x = centers[:, 0]
    y = centers[:, 1]
    z = data[var]
    msk = np.isfinite(z)
    x, y, z = x[msk], y[msk], z[msk]
    tri2 = Triangulation(x, y)
    fig, ax = plt.subplots(figsize=(7, 5))
    if clip:
        vmin, vmax = clip
    else:
        lo, hi = np.percentile(z, [2, 98])
        vmin, vmax = lo, hi
    tcf = ax.tricontourf(tri2, z, levels=40, vmin=vmin, vmax=vmax, cmap='jet')
    if zoom:
        ax.set_xlim(zoom[0], zoom[2])
        ax.set_ylim(zoom[1], zoom[3])
    ax.set_aspect('equal')
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_title('%s: %s' % (cid, title))
    cb = fig.colorbar(tcf, ax=ax)
    cb.set_label(var)
    fig.tight_layout()
    fig.savefig(os.path.join(out, 'figures', fname), dpi=130)
    plt.close(fig)
    return os.path.join(out, 'figures', fname)

def run(out, cid):
    os.makedirs(os.path.join(out, 'figures'), exist_ok=True)
    plot_residuals(out, cid)
    plot_forces(out, cid)
    plot_surface(out, cid)
    plot_field(out, cid, 'mach_number', 'mach.png', 'Mach number')
    plot_field(out, cid, 'pressure', 'pressure.png', 'pressure')
    if 're200' in cid.lower():
        plot_field(out, cid, 'speed', 'vorticity_wake.png', 'velocity magnitude (wake)', clip=[0, 1.5], zoom=[-2, -2, 5, 2])
    print("figures written to", os.path.join(out, 'figures'))
    return 0

if __name__ == "__main__":
    out = sys.argv[1]
    cid = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(out)
    sys.exit(run(out, cid))

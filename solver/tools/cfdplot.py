import os
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import xml.etree.ElementTree as ET

def read_vtu(path):
    tree = ET.parse(path)
    piece = tree.getroot().find("UnstructuredGrid").find("Piece")
    pts = np.array(piece.find("Points").find("DataArray").text.split(),
                   float).reshape(-1, 3)
    conn = off = types = None
    for da in piece.find("Cells").findall("DataArray"):
        nm = da.get("Name")
        if nm == "connectivity":
            conn = np.array(da.text.split(), int)
        elif nm == "offsets":
            off = np.array(da.text.split(), int)
        elif nm == "types":
            types = np.array(da.text.split(), int)
    fields = {}
    for da in piece.find("CellData").findall("DataArray"):
        fields[da.get("Name")] = np.array(da.text.split(), float)
    tris = []
    cellidx = []
    start = 0
    for c in range(len(types)):
        end = off[c]
        q = conn[start:end]
        if len(q) == 3:
            tris.append(q)
            cellidx.append(c)
        else:
            tris.append([q[0], q[1], q[2]])
            tris.append([q[0], q[2], q[3]])
            cellidx += [c, c]
        start = end
    return pts, np.array(tris), np.array(cellidx), fields

def tri_gradient(pts, tris, nodal_u, nodal_v):
    x = pts[:, 0]
    y = pts[:, 1]
    t = tris
    x0, x1, x2 = x[t[:, 0]], x[t[:, 1]], x[t[:, 2]]
    y0, y1, y2 = y[t[:, 0]], y[t[:, 1]], y[t[:, 2]]
    den = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    den = np.where(np.abs(den) < 1e-30, 1e-30, den)
    def grad(f):
        f0, f1, f2 = f[t[:, 0]], f[t[:, 1]], f[t[:, 2]]
        dfx = ((f1 - f0) * (y2 - y0) - (f2 - f0) * (y1 - y0)) / den
        dfy = ((x1 - x0) * (f2 - f0) - (x2 - x0) * (f1 - f0)) / den
        return dfx, dfy
    return grad

def cell_vorticity(pts, tris, cellidx, u_cell, v_cell):
    _, inv = np.unique(np.round(pts[:, :2], 12), axis=0, return_inverse=True)
    up = np.zeros((inv.max() + 1, 3))
    for c in range(inv.max() + 1):
        up[c] = pts[np.argmax(inv == c)]
    utris = inv[tris]
    nu = np.zeros(len(up))
    nv = np.zeros(len(up))
    cnt = np.zeros(len(up))
    for c in range(3):
        np.add.at(nu, utris[:, c], u_cell[cellidx])
        np.add.at(nv, utris[:, c], v_cell[cellidx])
        np.add.at(cnt, utris[:, c], 1.0)
    nu /= np.maximum(cnt, 1)
    nv /= np.maximum(cnt, 1)
    grad = tri_gradient(up, utris, nu, nv)
    dudx, dudy = grad(nu)
    dvdx, dvdy = grad(nv)
    return dvdx - dudy

def contour(pts, tris, vals, out, title, cmap="viridis", clim=None, xlim=None,
            ylim=None, figsize=(10, 8), levels=64):
    fig, ax = plt.subplots(figsize=figsize)
    if clim is not None:
        tpc = ax.tripcolor(pts[:, 0], pts[:, 1], tris, np.clip(vals, *clim),
                           shading="flat", cmap=cmap)
        tpc.set_clim(*clim)
    else:
        tpc = ax.tripcolor(pts[:, 0], pts[:, 1], tris, vals, shading="flat",
                           cmap=cmap)
    cb = fig.colorbar(tpc, ax=ax)
    cb.set_label(title, fontsize=12)
    ax.set_aspect("equal")
    ax.set_xlabel("x")
    ax.set_ylabel("y")
    if xlim:
        ax.set_xlim(*xlim)
    if ylim:
        ax.set_ylim(*ylim)
    ax.set_title(title, fontsize=12)
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)

def lineplot(xs, ys_list, labels, out, title, xlabel, ylabel, logy=False,
             figsize=(8, 5), xlabelpad=None):
    fig, ax = plt.subplots(figsize=figsize)
    for ys, lb in zip(ys_list, labels):
        if logy:
            ax.semilogy(xs, ys, label=lb, lw=1.4)
        else:
            ax.plot(xs, ys, label=lb, lw=1.4)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, alpha=0.4)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out, dpi=140)
    plt.close(fig)

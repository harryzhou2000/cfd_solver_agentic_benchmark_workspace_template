import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import xml.etree.ElementTree as ET

def read_vtu(path):
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find("UnstructuredGrid").find("Piece")
    pts_txt = piece.find("Points").find("DataArray").text.split()
    pts = np.array(pts_txt, dtype=float).reshape(-1, 3)
    conn = None
    types = None
    for da in piece.find("Cells").findall("DataArray"):
        if da.get("Name") == "connectivity":
            conn = np.array(da.text.split(), dtype=int)
        if da.get("Name") == "types":
            types = np.array(da.text.split(), dtype=int)
    fields = {}
    for da in piece.find("CellData").findall("DataArray"):
        fields[da.get("Name")] = np.array(da.text.split(), dtype=float)
    tris = []
    idx = 0
    for t in types:
        if t == 5:
            tris.append(conn[idx:idx+3])
            idx += 3
        else:
            q = conn[idx:idx+4]
            tris.append([q[0], q[1], q[2]])
            tris.append([q[0], q[2], q[3]])
            idx += 4
    tri = np.array(tris)
    vals = np.repeat(np.arange(len(types)), [3 if t == 5 else 4 for t in types])
    return pts, tri, np.array(vals), fields

pts, tri, cellnode, fields = read_vtu(sys.argv[1])
var = sys.argv[2] if len(sys.argv) > 2 else "Pressure"
v = fields[var][cellnode]
fig, ax = plt.subplots(figsize=(12, 8))
tpc = ax.tripcolor(pts[:, 0], pts[:, 1], tri, v, shading="flat")
fig.colorbar(tpc, ax=ax, label=var)
ax.set_aspect("equal")
ax.set_title(f"{var} min={v.min():.4g} max={v.max():.4g}")
fig.savefig(sys.argv[3] if len(sys.argv) > 3 else "viz.png", dpi=120)
print("saved")

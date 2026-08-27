"""Count vorticity sign reversals along a wake line, from a .vtu field file.

A directly measured count of alternating vortex cores is stronger evidence of a
resolved vortex street than an oscillating integrated force signal, which could in
principle come from a global mode without resolved individual vortices.
"""
import sys

sys.path.insert(0, "/workspace/solver/tools")
import numpy as np
from vtu_reader import read_vtu

path = sys.argv[1]
y_line = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
mesh = read_vtu(path)
vort = mesh.cell_array("vorticity")
centroids = mesh.cell_centroids()
cx, cy = centroids[:, 0], centroids[:, 1]

lo, hi = 1.0, 15.0
band = float(sys.argv[3]) if len(sys.argv) > 3 else 0.05
sel = (cx >= lo) & (cx <= hi) & (np.abs(cy - y_line) <= band)
order = np.argsort(cx[sel])
series = vort[sel][order]
xs = cx[sel][order]
print("vorticity range over whole field: %.3f .. %.3f" % (vort.min(), vort.max()))
print("cells sampled along y=%.2f, x in [%.0f,%.0f]: %d" % (y_line, lo, hi, series.size))
signs = np.sign(series)
signs = signs[signs != 0]
reversals = int(np.sum(signs[1:] != signs[:-1]))
print("vorticity sign reversals: %d" % reversals)

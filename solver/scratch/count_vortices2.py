"""Robust count of vortex cores in the near wake from a .vtu field.

Sampling scattered cell values along a line and sorting by x interleaves cells at
different y, which manufactures spurious sign changes.  Instead this bins the wake
into uniform x intervals, takes the vorticity of the cell nearest the sample line in
each bin, and counts sign reversals of that well-defined one-dimensional signal.
A physical expectation is available for comparison: at Strouhal number St the
streamwise vortex spacing is U/f = 1/St diameters, and along the centreline the
sign alternates every half spacing.
"""
import sys

sys.path.insert(0, "/workspace/solver/tools")
import numpy as np
from vtu_reader import read_vtu

path = sys.argv[1]
y_line = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
strouhal = float(sys.argv[3]) if len(sys.argv) > 3 else 0.182

mesh = read_vtu(path)
vort = mesh.cell_array("vorticity")
centroids = mesh.cell_centroids()
cx, cy = centroids[:, 0], centroids[:, 1]

lo, hi, nbins = 1.0, 15.0, 140
edges = np.linspace(lo, hi, nbins + 1)
series = []
for i in range(nbins):
    sel = (cx >= edges[i]) & (cx < edges[i + 1])
    if not np.any(sel):
        continue
    idx = np.argmin(np.abs(cy[sel] - y_line))
    series.append(vort[sel][idx])
series = np.asarray(series)

signs = np.sign(series)
signs = signs[signs != 0]
reversals = int(np.sum(signs[1:] != signs[:-1]))

print("vorticity range over field: %.3f .. %.3f" % (vort.min(), vort.max()))
print("bins with data along y=%.2f: %d over x in [%.0f,%.0f]" % (y_line, series.size, lo, hi))
print("measured sign reversals: %d" % reversals)
spacing = 1.0 / strouhal
print("expected from St=%.3f: spacing %.2f D, so about %.1f reversals over %.0f D"
      % (strouhal, spacing, (hi - lo) / (0.5 * spacing), hi - lo))

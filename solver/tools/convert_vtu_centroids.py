#!/usr/bin/env python3
"""Convert a legacy node-based field VTU into the centroid-point format.

The current solver writes ``field_final.vtu`` with the cell centroids as the
VTK points and one VTK_VERTEX per cell, so the cell-centered data arrays align
with the geometry.  Earlier solver versions wrote the mesh *nodes* as points
and real cells as connectivity, which made the CellData arrays longer than the
Points array and broke Delaunay-based contouring of the cell-centered fields.

This script rebuilds a legacy VTU in the centroid format without re-running the
solver: it reads the node coordinates and the cell connectivity, computes the
true (area-weighted) centroid of every cell, and writes the cell data arrays
unchanged.

Usage:
  python3 convert_vtu_centroids.py field_final.vtu [field_final.vtu ...]

The input file is replaced in place (a ``.bak`` copy is kept).
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np


def cell_centroid(nodes, conn):
    """Area-weighted centroid of a 2-D polygon given its vertex coordinates."""
    x = nodes[conn, 0]
    y = nodes[conn, 1]
    n = len(conn)
    if n == 3:
        return np.array([x.mean(), y.mean()])
    # Polygon fan from vertex 0 (exact for convex cells, incl. quads).
    a = 0.0
    cx = 0.0
    cy = 0.0
    for i in range(1, n - 1):
        ax, ay = x[0], y[0]
        bx, by = x[i], y[i]
        cx_, cy_ = x[i + 1], y[i + 1]
        area = 0.5 * ((bx - ax) * (cy_ - ay) - (cx_ - ax) * (by - ay))
        a += area
        cx += area * (ax + bx + cx_) / 3.0
        cy += area * (ay + by + cy_) / 3.0
    if abs(a) < 1e-30:
        return np.array([x.mean(), y.mean()])
    return np.array([cx / a, cy / a])


def convert(path: Path) -> None:
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//UnstructuredGrid/Piece")

    pts_da = root.find(".//UnstructuredGrid/Piece/Points/DataArray")
    ncomp = int(pts_da.get("NumberOfComponents", "3"))
    pts = np.array([float(v) for v in pts_da.text.split()]).reshape(-1, ncomp)

    def read_arrays(parent):
        out = {}
        for da in parent.findall("DataArray"):
            name = da.get("Name")
            ncomp_da = int(da.get("NumberOfComponents", "1"))
            vals = np.array([float(v) for v in da.text.split()])
            if ncomp_da > 1:
                vals = vals.reshape(-1, ncomp_da)
            out[name] = vals
        return out

    cells_arrays = read_arrays(piece.find("Cells"))
    celldata_arrays = read_arrays(piece.find("CellData"))
    conn = np.array([int(v) for v in cells_arrays["connectivity"].tolist()],
                    dtype=np.int64)
    offsets = np.array([int(v) for v in cells_arrays["offsets"].tolist()],
                       dtype=np.int64)

    n_cells = len(offsets)
    cell_data_names = list(celldata_arrays)
    if cell_data_names:
        n_data = len(celldata_arrays[cell_data_names[0]])
        if n_data != n_cells:
            raise SystemExit(
                f"{path}: cell data has {n_data} entries but {n_cells} cells")

    start = 0
    centroids = np.zeros((n_cells, 2))
    for c in range(n_cells):
        end = int(offsets[c])
        nodes = conn[start:end]
        start = end
        if len(nodes) < 3:
            raise SystemExit(f"{path}: cell {c} has {len(nodes)} nodes")
        centroids[c] = cell_centroid(pts, nodes)

    out = path.with_suffix(path.suffix + ".tmp")
    with open(out, "w") as f:
        f.write('<?xml version="1.0"?>\n')
        f.write('<VTKFile type="UnstructuredGrid" version="0.1" '
                'byte_order="LittleEndian">\n')
        f.write('  <UnstructuredGrid>\n')
        f.write(f'    <Piece NumberOfPoints="{n_cells}" '
                f'NumberOfCells="{n_cells}">\n')
        f.write('      <Points>\n')
        f.write('        <DataArray type="Float64" NumberOfComponents="3" '
                'format="ascii">\n')
        for cx, cy in centroids:
            f.write(f"{cx} {cy} 0\n")
        f.write('        </DataArray>\n      </Points>\n')
        f.write('      <Cells>\n')
        f.write('        <DataArray type="Int64" Name="connectivity" '
                'format="ascii">\n')
        f.write(" ".join(str(c) for c in range(n_cells)))
        f.write("\n        </DataArray>\n")
        f.write('        <DataArray type="Int64" Name="offsets" '
                'format="ascii">\n')
        f.write(" ".join(str(c + 1) for c in range(n_cells)))
        f.write("\n        </DataArray>\n")
        f.write('        <DataArray type="UInt8" Name="types" '
                'format="ascii">\n')
        f.write(" ".join("1" for _ in range(n_cells)))
        f.write("\n        </DataArray>\n      </Cells>\n")
        f.write('      <CellData>\n')
        for name in cell_data_names:
            vals = celldata_arrays[name]
            f.write(f'        <DataArray type="Float64" Name="{name}" '
                    'format="ascii">\n')
            if vals.ndim == 1:
                f.write(" ".join(repr(float(v)) for v in vals))
            else:
                for row in vals:
                    f.write(" ".join(repr(float(v)) for v in row) + "\n")
            f.write("\n        </DataArray>\n")
        f.write('      </CellData>\n')
        f.write('    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n')

    backup = path.with_suffix(path.suffix + ".bak")
    if not backup.exists():
        path.rename(backup)
    out.rename(path)
    print(f"converted {path}: {n_cells} centroid points")


def main() -> None:
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for arg in sys.argv[1:]:
        p = Path(arg)
        if not p.exists():
            raise SystemExit(f"missing file: {p}")
        convert(p)


if __name__ == "__main__":
    main()

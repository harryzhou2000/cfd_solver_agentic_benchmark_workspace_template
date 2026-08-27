"""Reader for the VTU files written by the solver.

The solver writes a standard XML VTK unstructured-grid file with a raw
appended data section (Float64 points, Int64 connectivity/offsets, UInt8 cell
types and Float32/Float64 cell data).  This module parses that layout without
requiring VTK or meshio to be installed, and also builds the triangulation used
by every field figure.
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field

import numpy as np

_DTYPES = {
    "Float32": np.float32,
    "Float64": np.float64,
    "Int32": np.int32,
    "Int64": np.int64,
    "UInt8": np.uint8,
    "UInt32": np.uint32,
    "UInt64": np.uint64,
}


@dataclass
class VtuMesh:
    points: np.ndarray                     # (npoints, 3)
    connectivity: np.ndarray               # flat node indices
    offsets: np.ndarray                    # end index of each cell
    types: np.ndarray                      # VTK cell types
    cell_data: dict = field(default_factory=dict)

    @property
    def n_cells(self) -> int:
        return len(self.offsets)

    def cell_nodes(self, i: int) -> np.ndarray:
        start = 0 if i == 0 else self.offsets[i - 1]
        return self.connectivity[start:self.offsets[i]]

    def cell_centers(self) -> np.ndarray:
        starts = np.concatenate(([0], self.offsets[:-1]))
        counts = self.offsets - starts
        xs = np.add.reduceat(self.points[self.connectivity, 0], starts) / counts
        ys = np.add.reduceat(self.points[self.connectivity, 1], starts) / counts
        return np.column_stack([xs, ys])

    def triangulate(self):
        """Split every cell into triangles.

        Returns (triangles, source_cell) where `source_cell[k]` is the cell that
        triangle k came from, so cell data can be mapped onto the triangulation.
        """
        tris = []
        src = []
        starts = np.concatenate(([0], self.offsets[:-1]))
        for c in range(self.n_cells):
            nodes = self.connectivity[starts[c]:self.offsets[c]]
            for k in range(1, len(nodes) - 1):
                tris.append((nodes[0], nodes[k], nodes[k + 1]))
                src.append(c)
        return np.asarray(tris, dtype=np.int64), np.asarray(src, dtype=np.int64)

    def cell_to_point(self, values: np.ndarray) -> np.ndarray:
        """Area-weighted cell-to-node averaging (for smooth contour plots)."""
        starts = np.concatenate(([0], self.offsets[:-1]))
        counts = self.offsets - starts
        areas = self.cell_areas()
        w = np.repeat(areas, counts)
        v = np.repeat(values * areas, counts)
        acc = np.zeros(len(self.points))
        wsum = np.zeros(len(self.points))
        np.add.at(acc, self.connectivity, v)
        np.add.at(wsum, self.connectivity, w)
        wsum[wsum == 0.0] = 1.0
        return acc / wsum

    def cell_areas(self) -> np.ndarray:
        if getattr(self, "_areas", None) is not None:
            return self._areas
        starts = np.concatenate(([0], self.offsets[:-1]))
        areas = np.empty(self.n_cells)
        px = self.points[:, 0]
        py = self.points[:, 1]
        for c in range(self.n_cells):
            n = self.connectivity[starts[c]:self.offsets[c]]
            x = px[n]
            y = py[n]
            areas[c] = 0.5 * abs(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))
        self._areas = areas
        return areas


def read_vtu(path: str) -> VtuMesh:
    with open(path, "rb") as f:
        raw = f.read()
    marker = raw.find(b"<AppendedData")
    if marker < 0:
        raise ValueError(f"{path}: no appended data section")
    start = raw.index(b"_", marker) + 1
    header_xml = raw[:marker].decode("utf-8", errors="replace") + "</VTKFile>"
    # The XML prefix stops mid-document; close the open elements before parsing.
    for tag in ("</AppendedData>",):
        header_xml = header_xml.replace(tag, "")
    header_xml = re.sub(r"<AppendedData[^>]*>", "", header_xml)
    root = ET.fromstring(header_xml)
    blob = raw[start:]

    piece = root.find(".//Piece")
    npoints = int(piece.attrib["NumberOfPoints"])
    ncells = int(piece.attrib["NumberOfCells"])

    def read_array(elem, expected_len):
        dtype = _DTYPES[elem.attrib["type"]]
        off = int(elem.attrib["offset"])
        nbytes = int(np.frombuffer(blob, dtype=np.uint64, count=1, offset=off)[0])
        data = np.frombuffer(blob, dtype=dtype, count=nbytes // dtype().itemsize,
                             offset=off + 8)
        if expected_len is not None and data.size != expected_len:
            raise ValueError(f"array {elem.attrib.get('Name')} has {data.size} entries, "
                             f"expected {expected_len}")
        return data

    pts_elem = piece.find("Points/DataArray")
    points = read_array(pts_elem, npoints * 3).reshape(npoints, 3).astype(np.float64)

    cells = {a.attrib["Name"]: a for a in piece.findall("Cells/DataArray")}
    offsets = read_array(cells["offsets"], ncells).astype(np.int64)
    connectivity = read_array(cells["connectivity"], int(offsets[-1])).astype(np.int64)
    types = read_array(cells["types"], ncells)

    cell_data = {}
    for a in piece.findall("CellData/DataArray"):
        cell_data[a.attrib["Name"]] = read_array(a, ncells).astype(np.float64)

    mesh = VtuMesh(points=points, connectivity=connectivity, offsets=offsets, types=types,
                   cell_data=cell_data)
    mesh._areas = None
    return mesh


if __name__ == "__main__":
    import sys

    m = read_vtu(sys.argv[1])
    print(f"points {len(m.points)}  cells {m.n_cells}")
    for k, v in m.cell_data.items():
        print(f"  {k:20s} min {v.min():14.6g}  max {v.max():14.6g}  mean {v.mean():14.6g}")

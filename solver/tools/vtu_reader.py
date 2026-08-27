"""NumPy-only reader for ASCII VTK XML UnstructuredGrid (.vtu) files.

This module deliberately avoids the VTK Python bindings and PyVista so that
post-processing works in a bare numpy + matplotlib environment.  It targets the
subset of the VTK XML format emitted by the 2-D CFD solver in this repository:

*   <VTKFile type="UnstructuredGrid"> with a single <Piece>.
*   format="ascii" data arrays (inline, whitespace-separated numbers).
*   Points with 3 components (z identically zero for a 2-D solver).
*   Cells with 'connectivity' (Int64), 'offsets' (Int64, cumulative *end*
    offsets) and 'types' (UInt8) holding VTK_TRIANGLE (5) and VTK_QUAD (9).
*   CellData arrays such as density, velocity (3 components), pressure, mach,
    temperature, total_energy, vorticity and rank.

Robustness notes
----------------
*   Numbers may be wrapped over any number of lines with arbitrary whitespace;
    parsing is purely token based.
*   The XML is parsed with xml.etree.ElementTree first.  If that fails (for
    example a stray character in a comment) the module falls back to a regular
    expression scan for <DataArray> blocks so a slightly malformed file can
    still be visualised.
*   'offsets' is accepted either as n_cells cumulative end offsets (the VTK
    convention) or as n_cells + 1 values with a leading zero.
*   Cell types other than triangles and quads are triangulated as generic
    polygons by fanning from the first vertex.

Typical use::

    from vtu_reader import read_vtu
    mesh = read_vtu("field_final.vtu")
    tri, tri_to_cell = mesh.triangulation()
    mach_nodal = mesh.cell_to_point(mesh.cell_array("mach"))
    ax.tricontourf(tri, mach_nodal, levels=40)
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

__all__ = [
    "VtuMesh",
    "VtuFormatError",
    "read_vtu",
    "VTK_TRIANGLE",
    "VTK_QUAD",
]

VTK_TRIANGLE = 5
VTK_QUAD = 9

#: VTK type id -> expected node count, for the types the solver writes.
_STRICT_NODE_COUNTS = {VTK_TRIANGLE: 3, VTK_QUAD: 4}


class VtuFormatError(RuntimeError):
    """Raised when a .vtu file cannot be interpreted by this reader."""


def _tokens_to_array(text: Optional[str], dtype) -> np.ndarray:
    """Convert a whitespace-separated ASCII data block into a numpy array.

    Tolerates arbitrary line wrapping, tabs and repeated spaces, and returns an
    empty array for an empty block.
    """
    if text is None:
        return np.empty(0, dtype=dtype)
    tokens = text.split()
    if not tokens:
        return np.empty(0, dtype=dtype)
    if np.issubdtype(np.dtype(dtype), np.integer):
        # Parse as float first so that "1.0"-style integers are tolerated.
        values = np.array(tokens, dtype=np.float64)
        if not np.all(np.isfinite(values)):
            raise VtuFormatError("non-finite value inside an integer data block")
        return np.rint(values).astype(dtype)
    return np.array(tokens, dtype=dtype)


def _numpy_dtype(vtk_type: str):
    """Map a VTK 'type=' attribute onto a numpy dtype."""
    integer_types = {
        "Int8", "UInt8", "Int16", "UInt16",
        "Int32", "UInt32", "Int64", "UInt64",
    }
    if vtk_type in integer_types:
        return np.int64
    return np.float64


@dataclass
class _RawArray:
    """A parsed <DataArray>: flat values plus its component count."""

    name: str
    values: np.ndarray
    num_components: int = 1

    def reshaped(self) -> np.ndarray:
        """Return the values shaped (rows,) or (rows, num_components)."""
        if self.num_components <= 1:
            return self.values
        if self.values.size % self.num_components != 0:
            raise VtuFormatError(
                "array '%s' holds %d values, not a multiple of "
                "NumberOfComponents=%d"
                % (self.name, self.values.size, self.num_components)
            )
        return self.values.reshape(-1, self.num_components)


@dataclass
class VtuMesh:
    """An unstructured 2-D mesh with cell- and point-centred data.

    Attributes
    ----------
    points:
        (n_points, 2) float array of node coordinates; the z column is dropped.
    cells:
        List of length n_cells; entry i holds the node indices of cell i in VTK
        winding order.
    cell_types:
        (n_cells,) int array of VTK cell type ids.
    cell_data, point_data:
        Name -> array dictionaries.  Scalars are (n,), k-component arrays such
        as 'velocity' are (n, k).
    path:
        Source file, kept for error messages.
    """

    points: np.ndarray
    cells: List[np.ndarray] = field(default_factory=list)
    cell_types: np.ndarray = field(default_factory=lambda: np.empty(0, dtype=np.int64))
    cell_data: Dict[str, np.ndarray] = field(default_factory=dict)
    point_data: Dict[str, np.ndarray] = field(default_factory=dict)
    path: Optional[Path] = None

    def __post_init__(self) -> None:
        self._size_groups: Optional[Dict[int, Tuple[np.ndarray, np.ndarray]]] = None
        self._areas: Optional[np.ndarray] = None
        self._centroids: Optional[np.ndarray] = None

    # ------------------------------------------------------------------ sizes
    @property
    def n_points(self) -> int:
        return int(self.points.shape[0])

    @property
    def n_cells(self) -> int:
        return len(self.cells)

    @property
    def x(self) -> np.ndarray:
        return self.points[:, 0]

    @property
    def y(self) -> np.ndarray:
        return self.points[:, 1]

    def bounds(self) -> Tuple[float, float, float, float]:
        """Return (xmin, xmax, ymin, ymax) of the node cloud."""
        return (
            float(self.points[:, 0].min()),
            float(self.points[:, 0].max()),
            float(self.points[:, 1].min()),
            float(self.points[:, 1].max()),
        )

    # --------------------------------------------------------------- geometry
    def _cells_by_size(self) -> Dict[int, Tuple[np.ndarray, np.ndarray]]:
        """Group cells by node count for vectorised per-cell work.

        Returns {n_nodes: (cell_indices, node_index_matrix)} so that areas,
        triangulation and averaging avoid a Python loop over every cell.
        """
        if self._size_groups is not None:
            return self._size_groups
        sizes = np.array([len(c) for c in self.cells], dtype=np.int64)
        groups: Dict[int, Tuple[np.ndarray, np.ndarray]] = {}
        for size in np.unique(sizes):
            idx = np.flatnonzero(sizes == size)
            if idx.size:
                matrix = np.stack([self.cells[i] for i in idx]).astype(np.int64)
            else:
                matrix = np.empty((0, int(size)), dtype=np.int64)
            groups[int(size)] = (idx, matrix)
        self._size_groups = groups
        return groups

    def cell_areas(self) -> np.ndarray:
        """Polygon areas from the shoelace formula, returned as magnitudes.

        Using magnitudes makes the weights valid for either winding direction.
        """
        if self._areas is not None:
            return self._areas
        areas = np.zeros(self.n_cells, dtype=np.float64)
        px, py = self.points[:, 0], self.points[:, 1]
        for size, (idx, matrix) in self._cells_by_size().items():
            if idx.size == 0 or size < 3:
                continue
            xs, ys = px[matrix], py[matrix]
            xs_next = np.roll(xs, -1, axis=1)
            ys_next = np.roll(ys, -1, axis=1)
            areas[idx] = 0.5 * np.abs(np.sum(xs * ys_next - xs_next * ys, axis=1))
        self._areas = areas
        return areas

    def cell_centroids(self) -> np.ndarray:
        """Return (n_cells, 2) vertex-average cell centres.

        The plain vertex average is used rather than the area centroid: for the
        well-shaped cells in these meshes the difference is invisible in a plot
        and this form stays finite for degenerate cells.
        """
        if self._centroids is not None:
            return self._centroids
        centroids = np.zeros((self.n_cells, 2), dtype=np.float64)
        for size, (idx, matrix) in self._cells_by_size().items():
            if idx.size == 0:
                continue
            centroids[idx] = self.points[matrix].mean(axis=1)
        self._centroids = centroids
        return centroids

    # ------------------------------------------------------------ data access
    def cell_array(self, name: str, component: Optional[int] = None) -> np.ndarray:
        """Fetch a cell array by name (case-insensitive), optionally one component."""
        arr = _lookup(self.cell_data, name)
        if arr is None:
            raise KeyError(
                "cell array '%s' not found in %s; available: %s"
                % (name, self.path, sorted(self.cell_data))
            )
        if component is not None and arr.ndim == 2:
            return arr[:, component]
        return arr

    def has_cell_array(self, name: str) -> bool:
        """True when a cell array of this name exists (case-insensitive)."""
        return _lookup(self.cell_data, name) is not None

    def velocity_magnitude(self) -> np.ndarray:
        """Cell-centred |u| from the 'velocity' array, or from 'u'/'v'."""
        vel = _lookup(self.cell_data, "velocity")
        if vel is not None:
            if vel.ndim == 1:
                return np.abs(vel)
            return np.sqrt(np.sum(np.asarray(vel[:, :2], dtype=np.float64) ** 2, axis=1))
        u = _lookup(self.cell_data, "u")
        v = _lookup(self.cell_data, "v")
        if u is None or v is None:
            raise KeyError(
                "cannot form velocity magnitude in %s; need a 'velocity' array or "
                "'u'/'v' arrays, have %s" % (self.path, sorted(self.cell_data))
            )
        return np.sqrt(np.asarray(u, dtype=np.float64) ** 2 + np.asarray(v, dtype=np.float64) ** 2)

    # ------------------------------------------------------- plotting helpers
    def triangulation(self):
        """Build a matplotlib Triangulation covering every area cell.

        Quads are split into two triangles along their shorter diagonal, which
        keeps mildly non-convex cells valid; a general n-gon is fanned from its
        first node.  Returns (triangulation, tri_to_cell) where tri_to_cell[k]
        is the cell that triangle k came from, so cell data can be attached to
        triangles directly (for example tripcolor(..., facecolors=...)).

        Degenerate (zero-area) triangles are dropped so contouring never sees a
        collapsed element.
        """
        from matplotlib.tri import Triangulation

        tris: List[np.ndarray] = []
        owners: List[np.ndarray] = []
        px, py = self.points[:, 0], self.points[:, 1]

        for size, (idx, matrix) in self._cells_by_size().items():
            if idx.size == 0 or size < 3:
                continue
            if size == 3:
                tris.append(matrix.copy())
                owners.append(idx.copy())
                continue
            if size == 4:
                n0, n1, n2, n3 = matrix[:, 0], matrix[:, 1], matrix[:, 2], matrix[:, 3]
                diag_02 = (px[n0] - px[n2]) ** 2 + (py[n0] - py[n2]) ** 2
                diag_13 = (px[n1] - px[n3]) ** 2 + (py[n1] - py[n3]) ** 2
                use_02 = (diag_02 <= diag_13)[:, None]
                first = np.where(
                    use_02,
                    np.stack([n0, n1, n2], axis=1),
                    np.stack([n0, n1, n3], axis=1),
                )
                second = np.where(
                    use_02,
                    np.stack([n0, n2, n3], axis=1),
                    np.stack([n1, n2, n3], axis=1),
                )
                tris.extend([first, second])
                owners.extend([idx.copy(), idx.copy()])
                continue
            for k in range(1, size - 1):
                tris.append(np.stack([matrix[:, 0], matrix[:, k], matrix[:, k + 1]], axis=1))
                owners.append(idx.copy())

        if not tris:
            raise VtuFormatError("no area cells to triangulate in %s" % self.path)

        triangles = np.concatenate(tris, axis=0).astype(np.int32)
        tri_to_cell = np.concatenate(owners, axis=0).astype(np.int64)

        ax_, ay_ = px[triangles[:, 0]], py[triangles[:, 0]]
        bx_, by_ = px[triangles[:, 1]], py[triangles[:, 1]]
        cx_, cy_ = px[triangles[:, 2]], py[triangles[:, 2]]
        double_area = np.abs((bx_ - ax_) * (cy_ - ay_) - (cx_ - ax_) * (by_ - ay_))
        scale = max(float(np.ptp(px)), float(np.ptp(py)), 1.0)
        good = double_area > 1.0e-14 * scale * scale
        if not np.all(good):
            triangles = triangles[good]
            tri_to_cell = tri_to_cell[good]
        if triangles.shape[0] == 0:
            raise VtuFormatError("every triangle degenerate in %s" % self.path)

        return Triangulation(px, py, triangles), tri_to_cell

    def cell_to_point(self, values: np.ndarray) -> np.ndarray:
        """Area-weighted cell-to-node interpolation.

        'values' may be (n_cells,) or (n_cells, k).  Each node receives
        sum(A_c * v_c) / sum(A_c) over the cells touching it, which is the
        smooth nodal field that tricontourf needs.  Nodes surrounded only by
        degenerate cells fall back to an unweighted mean, and any node that no
        cell references is filled with the global mean so contouring never sees
        a NaN.
        """
        values = np.asarray(values, dtype=np.float64)
        if values.shape[0] != self.n_cells:
            raise ValueError(
                "expected %d cell values, got %d" % (self.n_cells, values.shape[0])
            )
        squeeze = values.ndim == 1
        vals = values[:, None] if squeeze else values
        ncomp = vals.shape[1]

        areas = self.cell_areas()
        weights = np.where(np.isfinite(areas) & (areas > 0.0), areas, 0.0)

        weighted = np.zeros((self.n_points, ncomp), dtype=np.float64)
        plain_sum = np.zeros((self.n_points, ncomp), dtype=np.float64)
        weight_sum = np.zeros(self.n_points, dtype=np.float64)
        touch_count = np.zeros(self.n_points, dtype=np.float64)

        for size, (idx, matrix) in self._cells_by_size().items():
            if idx.size == 0:
                continue
            flat_nodes = matrix.reshape(-1)
            w = weights[idx]
            contrib = vals[idx]
            np.add.at(weight_sum, flat_nodes, np.repeat(w, size))
            np.add.at(touch_count, flat_nodes, 1.0)
            np.add.at(weighted, flat_nodes, np.repeat(w[:, None] * contrib, size, axis=0))
            np.add.at(plain_sum, flat_nodes, np.repeat(contrib, size, axis=0))

        out = np.zeros((self.n_points, ncomp), dtype=np.float64)
        ok = weight_sum > 0.0
        out[ok] = weighted[ok] / weight_sum[ok, None]
        fallback = (~ok) & (touch_count > 0.0)
        if np.any(fallback):
            out[fallback] = plain_sum[fallback] / touch_count[fallback, None]
        orphan = (~ok) & (touch_count <= 0.0)
        if np.any(orphan):
            out[orphan] = vals.mean(axis=0)
        return out[:, 0] if squeeze else out

    def summary(self) -> str:
        """A short human-readable description, handy for smoke tests."""
        xmin, xmax, ymin, ymax = self.bounds()
        kinds = {int(t): int(np.sum(self.cell_types == t)) for t in np.unique(self.cell_types)}
        lines = [
            "file      : %s" % self.path,
            "points    : %d" % self.n_points,
            "cells     : %d  types=%s" % (self.n_cells, kinds),
            "bounds    : x[%.4g, %.4g]  y[%.4g, %.4g]" % (xmin, xmax, ymin, ymax),
            "total area: %.6g" % self.cell_areas().sum(),
        ]
        for label, table in (("cell", self.cell_data), ("point", self.point_data)):
            for name in sorted(table):
                arr = table[name]
                kind = "scalar" if arr.ndim == 1 else "%d-vector" % arr.shape[1]
                lines.append(
                    "  %s/%-13s %-9s min=%.6g max=%.6g"
                    % (label, name, kind, np.nanmin(arr), np.nanmax(arr))
                )
        return "\n".join(lines)


def _lookup(table: Dict[str, np.ndarray], name: str) -> Optional[np.ndarray]:
    """Case-insensitive dictionary lookup returning None when absent."""
    if name in table:
        return table[name]
    lowered = name.lower()
    for key, value in table.items():
        if key.lower() == lowered:
            return value
    return None


_DATAARRAY_RE = re.compile(
    r"<DataArray\b(?P<attrs>[^>]*?)>(?P<body>.*?)</DataArray>", re.DOTALL | re.IGNORECASE
)
_ATTR_RE = re.compile(r'(\w+)\s*=\s*"([^"]*)"')
_SECTION_RE = re.compile(
    r"<(?P<tag>Points|Cells|CellData|PointData)\b[^>]*>(?P<body>.*?)</\1>",
    re.DOTALL | re.IGNORECASE,
)
_SECTION_NAMES = {
    "points": "Points",
    "cells": "Cells",
    "celldata": "CellData",
    "pointdata": "PointData",
}


def _raw_from_attrs(attrs: Dict[str, str], body: str) -> _RawArray:
    """Build a _RawArray from <DataArray> attributes plus its text body."""
    attrs = {k.lower(): v for k, v in attrs.items()}
    fmt = (attrs.get("format") or "ascii").strip().lower()
    name = attrs.get("name") or "unnamed"
    if fmt != "ascii":
        raise VtuFormatError(
            "DataArray '%s' uses format='%s'; this reader only supports "
            "format='ascii' (write the .vtu uncompressed with inline ASCII)"
            % (name, fmt)
        )
    dtype = _numpy_dtype(attrs.get("type", "Float64"))
    ncomp = int(float(attrs.get("numberofcomponents", 1) or 1))
    return _RawArray(name=name, values=_tokens_to_array(body, dtype), num_components=max(1, ncomp))


def _parse_with_elementtree(text: str) -> Dict[str, List[_RawArray]]:
    """Parse the document into {section: [_RawArray, ...]} using ElementTree."""
    root = ET.fromstring(text)
    grid = next((el for el in root.iter() if el.tag.split("}")[-1] == "UnstructuredGrid"), None)
    if grid is None:
        raise VtuFormatError("no <UnstructuredGrid> element found")
    pieces = [el for el in grid.iter() if el.tag.split("}")[-1] == "Piece"]
    if not pieces:
        raise VtuFormatError("no <Piece> element found")
    if len(pieces) > 1:
        raise VtuFormatError(
            "this reader supports a single <Piece>, found %d; write one piece per file"
            % len(pieces)
        )

    sections: Dict[str, List[_RawArray]] = {}
    for child in pieces[0]:
        tag = child.tag.split("}")[-1]
        if tag not in _SECTION_NAMES.values():
            continue
        for da in child:
            if da.tag.split("}")[-1] != "DataArray":
                continue
            sections.setdefault(tag, []).append(_raw_from_attrs(dict(da.attrib), da.text or ""))
    return sections


def _parse_with_regex(text: str) -> Dict[str, List[_RawArray]]:
    """Fallback parser locating sections and <DataArray> blocks textually."""
    sections: Dict[str, List[_RawArray]] = {}
    for match in _SECTION_RE.finditer(text):
        canonical = _SECTION_NAMES[match.group("tag").lower()]
        for da in _DATAARRAY_RE.finditer(match.group("body")):
            attrs = dict(_ATTR_RE.findall(da.group("attrs")))
            sections.setdefault(canonical, []).append(_raw_from_attrs(attrs, da.group("body")))
    if not sections:
        raise VtuFormatError("no Points/Cells sections could be located")
    return sections


def _build_cells(
    connectivity: np.ndarray, offsets: np.ndarray, types: np.ndarray
) -> Tuple[List[np.ndarray], np.ndarray]:
    """Turn flat connectivity/offsets/types into a per-cell node-index list."""
    offsets = np.asarray(offsets, dtype=np.int64)
    types = np.asarray(types, dtype=np.int64)
    conn = np.asarray(connectivity, dtype=np.int64)

    n_cells = int(types.size)
    if offsets.size == n_cells + 1 and offsets.size > 0 and int(offsets[0]) == 0:
        # Tolerate writers that emit a leading zero; keep end offsets only.
        offsets = offsets[1:]
    if n_cells == 0 and offsets.size:
        n_cells = int(offsets.size)
        types = np.full(n_cells, -1, dtype=np.int64)
    if offsets.size != n_cells:
        raise VtuFormatError(
            "offsets length %d does not match cell count %d" % (offsets.size, n_cells)
        )
    if n_cells == 0:
        return [], types
    if int(offsets[-1]) != conn.size:
        raise VtuFormatError(
            "last offset %d does not match connectivity length %d; offsets must be "
            "cumulative END offsets" % (int(offsets[-1]), conn.size)
        )
    if np.any(np.diff(offsets) < 0):
        raise VtuFormatError("offsets must be non-decreasing")

    starts = np.concatenate(([0], offsets[:-1]))
    cells: List[np.ndarray] = []
    for start, end, ctype in zip(starts, offsets, types):
        nodes = conn[int(start) : int(end)]
        expected = _STRICT_NODE_COUNTS.get(int(ctype))
        if expected is not None and nodes.size != expected:
            raise VtuFormatError(
                "cell of VTK type %d should have %d nodes, got %d"
                % (int(ctype), expected, nodes.size)
            )
        cells.append(nodes)
    return cells, types


def read_vtu(path) -> VtuMesh:
    """Read an ASCII VTK XML unstructured grid and return a VtuMesh.

    Raises VtuFormatError with an actionable message when the file uses an
    unsupported encoding (binary, appended or compressed) or when the
    connectivity is internally inconsistent.
    """
    path = Path(path)
    if not path.exists():
        raise FileNotFoundError("no such .vtu file: %s" % path)
    text = path.read_text(errors="replace")

    head = text[:4096].lower()
    if "appendeddata" in head or 'format="appended"' in head:
        raise VtuFormatError(
            "%s contains an AppendedData section; write inline ASCII DataArrays instead" % path
        )
    if "compressor=" in head:
        raise VtuFormatError("%s declares a compressor; write the file uncompressed in ASCII" % path)

    try:
        sections = _parse_with_elementtree(text)
    except ET.ParseError:
        sections = _parse_with_regex(text)

    if not sections.get("Points"):
        raise VtuFormatError("%s has no <Points> DataArray" % path)

    pts = sections["Points"][0].reshaped()
    if pts.ndim == 1:
        pts = pts.reshape(-1, 1)
    if pts.shape[1] < 2:
        raise VtuFormatError("%s: points need at least 2 components" % path)
    points = np.ascontiguousarray(pts[:, :2], dtype=np.float64)

    cell_section = {a.name.lower(): a for a in sections.get("Cells", [])}
    missing = [k for k in ("connectivity", "offsets") if k not in cell_section]
    if missing:
        raise VtuFormatError("%s: <Cells> is missing %s" % (path, missing))
    connectivity = cell_section["connectivity"].values.astype(np.int64)
    offsets = cell_section["offsets"].values.astype(np.int64)
    types = (
        cell_section["types"].values.astype(np.int64)
        if "types" in cell_section
        else np.empty(0, dtype=np.int64)
    )
    cells, cell_types = _build_cells(connectivity, offsets, types)

    if connectivity.size and (
        connectivity.min() < 0 or connectivity.max() >= points.shape[0]
    ):
        raise VtuFormatError(
            "%s: connectivity references a node index outside [0, %d]"
            % (path, points.shape[0] - 1)
        )

    def collect(tag: str, expected_rows: int) -> Dict[str, np.ndarray]:
        table: Dict[str, np.ndarray] = {}
        for raw in sections.get(tag, []):
            arr = raw.reshaped()
            if arr.shape[0] != expected_rows:
                raise VtuFormatError(
                    "%s: %s array '%s' has %d entries, expected %d"
                    % (path, tag, raw.name, arr.shape[0], expected_rows)
                )
            table[raw.name] = arr
        return table

    return VtuMesh(
        points=points,
        cells=cells,
        cell_types=cell_types,
        cell_data=collect("CellData", len(cells)),
        point_data=collect("PointData", points.shape[0]),
        path=path,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Command line entry point: summarise one or more .vtu files."""
    import argparse

    parser = argparse.ArgumentParser(description="Inspect ASCII VTK .vtu files.")
    parser.add_argument("files", nargs="+", help=".vtu files to summarise")
    args = parser.parse_args(argv)
    for name in args.files:
        mesh = read_vtu(name)
        print(mesh.summary())
        tri, _ = mesh.triangulation()
        print("  triangles : %d (from %d cells)" % (tri.triangles.shape[0], mesh.n_cells))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

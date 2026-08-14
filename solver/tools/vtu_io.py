#!/usr/bin/env python3
"""Lightweight VTU reader for solver-generated ASCII unstructured grids."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np


@dataclass
class VtuMesh:
    points: np.ndarray
    connectivity: np.ndarray
    offsets: np.ndarray
    types: np.ndarray
    cell_data: dict[str, np.ndarray]

    def triangulated_connectivity(self) -> np.ndarray:
        """Return triangle connectivity suitable for Matplotlib triangulation."""
        tris: list[list[int]] = []
        start = 0
        for end, ctype in zip(self.offsets, self.types):
            nodes = self.connectivity[start:end]
            start = end
            if ctype == 5:  # triangle
                tris.append([nodes[0], nodes[1], nodes[2]])
            elif ctype == 9:  # quad -> two triangles
                tris.append([nodes[0], nodes[1], nodes[2]])
                tris.append([nodes[0], nodes[2], nodes[3]])
        return np.asarray(tris, dtype=int)


def read_vtu(path: str | Path) -> VtuMesh:
    path = Path(path)
    root = ET.parse(path).getroot()
    piece = root.find("./UnstructuredGrid/Piece")
    if piece is None:
        raise ValueError(f"no VTK Piece found in {path}")

    def array_text(name: str | None = None, parent: ET.Element | None = None):
        if parent is None:
            raise ValueError("parent required")
        for da in parent.findall("DataArray"):
            if name is None or da.attrib.get("Name") == name:
                return da.text or ""
        raise ValueError(f"DataArray {name} missing in {path}")

    points = np.fromstring(array_text(parent=piece.find("Points")), sep=" ", dtype=float)
    points = points.reshape(-1, 3)
    cells = piece.find("Cells")
    connectivity = np.fromstring(array_text("connectivity", cells), sep=" ", dtype=np.int64)
    offsets = np.fromstring(array_text("offsets", cells), sep=" ", dtype=np.int64)
    types = np.fromstring(array_text("types", cells), sep=" ", dtype=np.int64)

    cell_data: dict[str, np.ndarray] = {}
    cell_node = piece.find("CellData")
    if cell_node is not None:
        for da in cell_node.findall("DataArray"):
            name = da.attrib.get("Name")
            if not name:
                continue
            cell_data[name] = np.fromstring(da.text or "", sep=" ", dtype=float)
    return VtuMesh(points, connectivity, offsets, types, cell_data)

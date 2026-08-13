"""Shared parsing helpers for solver CSV/JSON/VTU outputs."""

from __future__ import annotations

import csv
import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path


def read_csv(path: Path) -> list[dict]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def read_json(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def parse_vtu(path: Path) -> dict[str, list[float]]:
    """Parse a simple ASCII VTU written by cfd_fv2d (disconnected polygon
    cells). Returns cell data arrays plus corner coordinates."""
    root = ET.parse(path).getroot()
    piece = root.find(".//Piece")
    npoints = int(piece.attrib["NumberOfPoints"])
    ncells = int(piece.attrib["NumberOfCells"])
    points_txt = piece.find(".//Points/DataArray").text
    vals = [float(v) for v in points_txt.split()]
    xs = vals[0::3]
    ys = vals[1::3]
    assert len(xs) == npoints
    offsets = [
        int(v)
        for v in piece.find(".//Cells/DataArray[@Name='offsets']")
        .text.split()
    ]
    assert len(offsets) == ncells
    cell_x, cell_y = [], []
    prev = 0
    for off in offsets:
        cell_x.append(xs[prev:off])
        cell_y.append(ys[prev:off])
        prev = off
    data: dict[str, list[float]] = {"cell_x": cell_x, "cell_y": cell_y}
    for arr in piece.findall(".//CellData/DataArray"):
        data[arr.attrib["Name"]] = [float(v) for v in arr.text.split()]
    return data


def split_cells_to_tris(data: dict[str, list[float]], field: str):
    """Split polygonal cell data into triangles for Matplotlib Triangulation."""
    xs, ys, vals = [], [], []
    for px, py, v in zip(data["cell_x"], data["cell_y"], data[field]):
        n = len(px)
        for k in range(1, n - 1):
            xs.extend([px[0], px[k], px[k + 1]])
            ys.extend([py[0], py[k], py[k + 1]])
            vals.extend([v, v, v])
    return xs, ys, vals


def clip_percentiles(values: list[float], lo: float, hi: float):
    import numpy as np

    arr = np.asarray(values, dtype=float)
    arr = arr[np.isfinite(arr)]
    if arr.size == 0:
        return (0.0, 1.0)
    return tuple(float(v) for v in np.percentile(arr, [lo, hi]))

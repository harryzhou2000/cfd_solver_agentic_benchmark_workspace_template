"""Shared utilities for post-processing cfd2d benchmark outputs."""

from __future__ import annotations

import csv
import json
import math
from pathlib import Path

import numpy as np


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def read_csv_cols(path: Path) -> dict[str, np.ndarray]:
    rows = read_csv_rows(path)
    out: dict[str, list[float]] = {k: [] for k in rows[0]}
    for r in rows:
        for k, v in r.items():
            try:
                out[k].append(float(v))
            except ValueError:
                out[k].append(np.nan)
    return {k: np.asarray(v) for k, v in out.items()}


def load_vtu(path: Path):
    """Parse an ASCII .vtu file written by cfd2d.

    Returns (points [n,3] float, cells list of int arrays, cell_data dict).
    """
    import xml.etree.ElementTree as ET

    tree = ET.parse(str(path))
    root = tree.getroot()
    piece = root.find("UnstructuredGrid/Piece")
    n_points = int(piece.get("NumberOfPoints"))
    n_cells = int(piece.get("NumberOfCells"))

    pts_da = piece.find("Points/DataArray")
    points = np.fromstring(pts_da.text, sep=" ").reshape(n_points, 3)

    cells_el = piece.find("Cells")
    conn = None
    offsets = None
    for da in cells_el.findall("DataArray"):
        name = da.get("Name")
        if name == "connectivity":
            conn = np.fromstring(da.text, sep=" ", dtype=np.int64)
        elif name == "offsets":
            offsets = np.fromstring(da.text, sep=" ", dtype=np.int64)

    cells = []
    prev = 0
    for off in offsets:
        cells.append(conn[prev:off])
        prev = off

    cell_data = {}
    cd_el = piece.find("CellData")
    for da in cd_el.findall("DataArray"):
        cell_data[da.get("Name")] = np.fromstring(da.text, sep=" ")

    assert len(cells) == n_cells
    return points, cells, cell_data


def cells_to_triangles(cells) -> np.ndarray:
    """Fan-triangulate polygonal cells (tri -> 1, quad -> 2)."""
    tris = []
    for c in cells:
        if len(c) == 3:
            tris.append((c[0], c[1], c[2]))
        else:
            for k in range(1, len(c) - 1):
                tris.append((c[0], c[k], c[k + 1]))
    return np.asarray(tris, dtype=np.int64)


def cell_to_node(points: np.ndarray, cells, cdata: np.ndarray) -> np.ndarray:
    """Area-weighted average of cell data to nodes (for smooth contours)."""
    n = points.shape[0]
    acc = np.zeros(n)
    wsum = np.zeros(n)
    for c, v in zip(cells, cdata):
        pts = points[list(c), :2]
        area = 0.5 * abs(
            np.dot(pts[:, 0], np.roll(pts[:, 1], -1))
            - np.dot(pts[:, 1], np.roll(pts[:, 0], -1))
        )
        for nd in c:
            acc[nd] += v * max(area, 1e-30)
            wsum[nd] += max(area, 1e-30)
    return acc / np.maximum(wsum, 1e-30)


def spectral_stats(time: np.ndarray, sig: np.ndarray, t_start: float):
    """Dominant frequency, mean, and amplitude of a periodic signal for
    t >= t_start via a Hann-windowed FFT."""
    mask = time >= t_start
    t = time[mask]
    s = sig[mask]
    if len(t) < 16:
        return {"mean": float(np.mean(s)) if len(s) else float("nan"),
                "amplitude": float(np.ptp(s) / 2) if len(s) else float("nan"),
                "frequency": float("nan")}
    dt = float(np.median(np.diff(t)))
    s0 = s - np.mean(s)
    n = len(s0)
    win = np.hanning(n)
    spec = np.fft.rfft(s0 * win)
    freqs = np.fft.rfftfreq(n, d=dt)
    amp_spec = 2.0 * np.abs(spec) / (np.sum(win) * 0.5)
    k = int(np.argmax(np.abs(spec[1:])) + 1)
    return {"mean": float(np.mean(s)), "amplitude": float(amp_spec[k]),
            "frequency": float(freqs[k])}

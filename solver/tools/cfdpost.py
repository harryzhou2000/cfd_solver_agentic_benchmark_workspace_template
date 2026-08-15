"""Shared post-processing helpers for fv2d solver outputs."""
from __future__ import annotations

import csv
import json
import math
from pathlib import Path

import numpy as np


def read_csv_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def col(rows, name, cast=float):
    return np.array([cast(r[name]) for r in rows])


class VtkField:
    """Minimal legacy ASCII VTK UNSTRUCTURED_GRID reader (tri/quad cells)."""

    def __init__(self, path):
        lines = Path(path).read_text().split("\n")
        i = 0
        while not lines[i].startswith("POINTS"):
            i += 1
        n = int(lines[i].split()[1])
        i += 1
        pts = np.array([[float(v) for v in lines[i + k].split()] for k in range(n)])
        i += n
        self.points = pts[:, :2]
        while not lines[i].startswith("CELLS"):
            i += 1
        nc, _ = map(int, lines[i].split()[1:3])
        i += 1
        cells = [list(map(int, lines[i + k].split()[1:])) for k in range(nc)]
        i += nc
        while not lines[i].startswith("CELL_TYPES"):
            i += 1
        i += 1
        ctypes = [int(lines[i + k]) for k in range(nc)]
        i += nc
        while not lines[i].startswith("CELL_DATA"):
            i += 1
        i += 1
        self.cells = cells
        self.ncells = nc
        self.cell_data = {}
        self.vectors = {}
        while i < len(lines):
            if lines[i].startswith("SCALARS"):
                name = lines[i].split()[1]
                i += 2
                self.cell_data[name] = np.array([float(lines[i + k]) for k in range(nc)])
                i += nc
            elif lines[i].startswith("VECTORS"):
                name = lines[i].split()[1]
                i += 1
                self.vectors[name] = np.array(
                    [[float(v) for v in lines[i + k].split()] for k in range(nc)]
                )
                i += nc
            else:
                i += 1
        self.cell_centers = np.array([self.points[c].mean(axis=0) for c in cells])
        # triangulation for contouring (quads split into 2 tris)
        tris = []
        tri_cell = []
        for ci, c in enumerate(cells):
            if len(c) == 3:
                tris.append(c)
                tri_cell.append(ci)
            else:
                tris.append([c[0], c[1], c[2]])
                tri_cell.append(ci)
                tris.append([c[0], c[2], c[3]])
                tri_cell.append(ci)
        self.tris = np.array(tris)
        self.tri_cell = np.array(tri_cell)

    def flat_value(self, cell_values):
        """Cell values expanded to per-split-triangle for flat shading."""
        return np.asarray(cell_values)[self.tri_cell]

    def node_value(self, cell_values):
        """Average cell values onto nodes for smooth contouring."""
        acc = np.zeros(len(self.points))
        cnt = np.zeros(len(self.points))
        for c, v in zip(self.cells, cell_values):
            for n in c:
                acc[n] += v
                cnt[n] += 1
        return acc / np.maximum(cnt, 1)


def load_case(output_dir):
    d = Path(output_dir)
    out = {"dir": d}
    out["metadata"] = json.loads((d / "metadata.json").read_text())
    out["status"] = json.loads((d / "run_status.json").read_text())
    out["residuals"] = read_csv_rows(d / "residuals.csv")
    out["forces"] = read_csv_rows(d / "forces.csv")
    out["surface"] = read_csv_rows(d / "surface.csv")
    vtk = sorted(d.glob("field_final.*"))
    if vtk:
        out["field"] = VtkField(vtk[0])
    return out


def strouhal_from_lift(t, cl, t_min):
    """Estimate shedding frequency/Strouhal from cl(t) past t_min via FFT."""
    t = np.asarray(t)
    cl = np.asarray(cl)
    mask = t >= t_min
    tt = t[mask]
    cc = cl[mask]
    if len(tt) < 64:
        return None, None, None, None
    dt = np.mean(np.diff(tt))
    cc = cc - cc.mean()
    # Hann window
    cc = cc * np.hanning(len(cc))
    n = len(cc)
    fft = np.abs(np.fft.rfft(cc))
    freqs = np.fft.rfftfreq(n, dt)
    if len(fft) < 2:
        return None, None, None, None
    k = int(np.argmax(fft[1:]) + 1)
    f_peak = freqs[k]
    st = f_peak  # St = f D / U = f since D=U=1 nondim
    amp = 2.0 * fft[k] / n
    return f_peak, st, amp, (freqs, fft)

"""Minimal pure-stdlib reader for the ASCII VTU files written by cfd_solver."""

import math
import re
import xml.etree.ElementTree as ET


def read_vtu(path):
    """Return dict with points, cells (list of node-id lists), and cell_data."""
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find(".//Piece")
    out = {
        "points": [],
        "cells": [],
        "cell_data": {},
        "cell_offsets": [],
        "cell_types": [],
    }
    pts = piece.find(".//Points/DataArray")
    if pts is not None:
        for line in (pts.text or "").strip().splitlines():
            v = line.split()
            if v:
                out["points"].append(tuple(float(x) for x in v[:3]))
    conn = piece.find(".//Cells/DataArray[@Name='connectivity']")
    offs = piece.find(".//Cells/DataArray[@Name='offsets']")
    types = piece.find(".//Cells/DataArray[@Name='types']")
    if conn is not None:
        ids = [int(x) for x in (conn.text or "").split()]
        off_list = [int(x) for x in (offs.text or "").split()] if offs is not None else []
        typ_list = [int(x) for x in (types.text or "").split()] if types is not None else []
        start = 0
        for off in off_list:
            out["cells"].append(ids[start:off])
            start = off
        out["cell_offsets"] = off_list
        out["cell_types"] = typ_list
    for da in piece.findall(".//CellData/DataArray"):
        name = da.get("Name")
        ncomp = int(da.get("NumberOfComponents", "1"))
        vals = []
        for line in (da.text or "").strip().splitlines():
            v = line.split()
            if not v:
                continue
            if ncomp == 1:
                vals.append(float(v[0]))
            else:
                vals.append(tuple(float(x) for x in v[:ncomp]))
        out["cell_data"][name] = vals
    return out


def cell_centroids(vtu):
    pts = vtu["points"]
    out = []
    for cell in vtu["cells"]:
        xs = [pts[i][0] for i in cell]
        ys = [pts[i][1] for i in cell]
        out.append((sum(xs) / len(xs), sum(ys) / len(ys)))
    return out


def poly_area(pts, cell):
    a = 0.0
    n = len(cell)
    for k in range(n):
        x0, y0 = pts[cell[k]][0], pts[cell[k]][1]
        x1, y1 = pts[cell[(k + 1) % n]][0], pts[cell[(k + 1) % n]][1]
        a += x0 * y1 - x1 * y0
    return 0.5 * a

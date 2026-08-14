#!/usr/bin/env python3
"""Minimal VTU (raw appended binary) reader for solver field files."""
import numpy as np
import re
import struct
import sys


def read_vtu(path):
    data = open(path, "rb").read()
    head_end = data.index(b"\n_")
    header = data[:head_end].decode()
    appended = data[head_end + 2 :]
    # find all DataArray entries with offsets in order of appearance
    entries = []
    for m in re.finditer(
        r'<DataArray type="(\w+)"(?: NumberOfComponents="(\d+)")? Name="(\w+)" format="appended" offset="(\d+)"/>',
        header,
    ):
        dtype, ncomp, name, offset = m.group(1), m.group(2), m.group(3), int(m.group(4))
        entries.append((name, dtype, int(ncomp) if ncomp else 1, offset))
    # points array has no Name match for "Points" naming; handle separately
    if not any(e[0] == "Points" for e in entries):
        m = re.search(
            r'<DataArray type="(\w+)" NumberOfComponents="(\d+)" format="appended" offset="(\d+)"/>',
            header,
        )
        dtype, ncomp, offset = m.group(1), int(m.group(2)), int(m.group(3))
        entries.append(("Points", dtype, ncomp, offset))

    def parse(entry):
        name, dtype, ncomp, offset = entry
        n = struct.unpack_from("<Q", appended, offset)[0]
        raw = appended[offset + 8 : offset + 8 + n]
        dt = {"Float64": np.float64, "Int32": np.int32, "UInt8": np.uint8}[dtype]
        arr = np.frombuffer(raw, dtype=dt)
        return arr.reshape(-1, ncomp) if ncomp > 1 else arr

    out = {}
    for e in entries:
        out[e[0]] = parse(e)
    return out


if __name__ == "__main__":
    f = read_vtu(sys.argv[1])
    pts = f["Points"]
    conn = f["connectivity"]
    off = f["offsets"]
    types = f["types"]
    ncell = len(off)
    cx = np.zeros(ncell)
    cy = np.zeros(ncell)
    start = 0
    for c in range(ncell):
        ids = conn[start : off[c]]
        cx[c] = pts[ids, 0].mean()
        cy[c] = pts[ids, 1].mean()
        start = off[c]
    names = sys.argv[2] if len(sys.argv) > 2 else "pressure"
    vals = f[names]
    # print cells near a location
    x0, y0 = float(sys.argv[3]), float(sys.argv[4])
    r = float(sys.argv[5]) if len(sys.argv) > 5 else 0.05
    sel = (np.abs(cx - x0) < r) & (np.abs(cy - y0) < r)
    idx = np.where(sel)[0]
    order = np.argsort(cx[idx])
    for c in idx[order][:40]:
        print(
            f"cell {c:6d} ({cx[c]:8.4f},{cy[c]:8.4f}) {names}={vals[c]:10.4f} "
            f"rho={f['density'][c]:7.4f} u={f['velocity_u'][c]:7.3f} v={f['velocity_v'][c]:7.3f} p={f['pressure'][c]:8.3f}"
        )

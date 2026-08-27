"""Minimal legacy VTK (BINARY/ASCII) unstructured-grid reader for fv2d fields."""
import numpy as np


def _read_ascii_tokens(f):
    for line in f:
        for tok in line.split():
            yield tok


def read_vtk(path):
    """Return dict with points (n,3), cells (list of node index arrays), celltypes,
    and cell_data dict of name -> array."""
    with open(path, "rb") as fb:
        data = fb.read()
    # tokenize header lines, then read binary payloads
    pos = 0

    def readline():
        nonlocal pos
        end = data.index(b"\n", pos)
        line = data[pos:end].decode()
        pos = end + 1
        return line

    assert readline().startswith("# vtk")
    readline()  # title
    mode = readline().strip().upper()
    assert mode in ("BINARY", "ASCII")
    assert readline().strip().upper() == "DATASET UNSTRUCTURED_GRID"
    tok = readline().split()
    assert tok[0] == "POINTS"
    np_ = int(tok[1])
    if mode == "BINARY":
        pts = np.frombuffer(data[pos:pos + 12 * np_], dtype=">f4").reshape(np_, 3).astype(np.float64)
        pos += 12 * np_
    else:
        vals = np.fromstring(data[pos:].decode().split("\n", 1)[0], sep=" ")
        raise NotImplementedError
    readline()  # newline after binary block
    tok = readline().split()
    assert tok[0] == "CELLS"
    nc, nconn = int(tok[1]), int(tok[2])
    conn = np.frombuffer(data[pos:pos + 4 * nconn], dtype=">i4").astype(np.int64)
    pos += 4 * nconn
    readline()
    tok = readline().split()
    assert tok[0] == "CELL_TYPES"
    ctypes = np.frombuffer(data[pos:pos + 4 * nc], dtype=">i4").astype(np.int32)
    pos += 4 * nc
    readline()
    # build cell list
    cells = []
    off = 0
    for i in range(nc):
        nn = conn[off]
        cells.append(conn[off + 1:off + 1 + nn])
        off += 1 + nn
    tok = readline().split()
    assert tok[0] == "CELL_DATA"
    cell_data = {}
    while pos < len(data) - 1:
        try:
            tok = readline().split()
        except Exception:
            break
        if not tok:
            continue
        if tok[0] == "SCALARS":
            name = tok[1]
            dtype = tok[2]
            readline()  # LOOKUP_TABLE
            if dtype == "float":
                arr = np.frombuffer(data[pos:pos + 4 * nc], dtype=">f4").astype(np.float64)
                pos += 4 * nc
            else:
                arr = np.frombuffer(data[pos:pos + 4 * nc], dtype=">i4").astype(np.int64)
                pos += 4 * nc
            cell_data[name] = arr
            readline()
        elif tok[0] == "VECTORS":
            name = tok[1]
            arr = np.frombuffer(data[pos:pos + 12 * nc], dtype=">f4").reshape(nc, 3).astype(np.float64)
            pos += 12 * nc
            cell_data[name] = arr
            readline()
        else:
            break
    return {"points": pts, "cells": cells, "celltypes": ctypes, "cell_data": cell_data}

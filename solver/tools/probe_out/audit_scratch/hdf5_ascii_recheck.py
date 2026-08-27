#!/usr/bin/env python3
"""Second, INDEPENDENT pass: compare interface coords using h5dump ASCII text
(decimal strings) instead of the raw binary blobs, to rule out a bug in my
own binary offset arithmetic. Also recompute NACA CoordinateZ stats from text.
"""
import struct, math, subprocess, os

SC = "/workspace/solver/tools/probe_out/audit_scratch"
H5 = "/opt/external/cfd_externals/install/bin/h5dump"
M = "/workspace/cfd_solver_agentic_benchmark/inputs/meshes"
ENV = dict(os.environ, LD_LIBRARY_PATH="/opt/external/cfd_externals/install/lib")

def ascii_vals(path):
    with open(path) as fh:
        return [ln.strip() for ln in fh if ln.strip()]

def dump_ints(f, p):
    out = subprocess.run([H5, "-d", p, "-A", "0", f], capture_output=True,
                         text=True, env=ENV).stdout
    vals = []
    for ln in out.splitlines():
        s = ln.strip()
        if s.startswith("(") and "):" in s:
            vals.append(int(s.split("):", 1)[1].strip().rstrip(",")))
    return vals

TXT = {}
for z in ("dom-1", "dom-2"):
    for c in ("CoordinateX", "CoordinateY"):
        TXT[(z, c)] = ascii_vals("%s/hdf5_ascii_%s_%s.txt" % (SC, z, c))

CYL = M + "/CylinderB1.cgns"
DONOR = {"dom-1": "dom-2", "dom-2": "dom-1"}
tot = bad_txt = 0
worst = 0.0
for z in ("dom-1", "dom-2"):
    dz = DONOR[z]
    for c in ("con-2", "con-3", "con-4", "con-5", "con-6"):
        b = "/Base/%s/ZoneGridConnectivity/1to1Connection:%s" % (z, c)
        pl = dump_ints(CYL, b + "/PointList/ data")
        pd = dump_ints(CYL, b + "/PointListDonor/ data")
        for i in range(len(pl)):
            a, d = pl[i] - 1, pd[i] - 1
            sxa, sxb = TXT[(z, "CoordinateX")][a], TXT[(dz, "CoordinateX")][d]
            sya, syb = TXT[(z, "CoordinateY")][a], TXT[(dz, "CoordinateY")][d]
            tot += 1
            if sxa != sxb or sya != syb:
                bad_txt += 1
                if bad_txt <= 5:
                    print("  TEXT MISMATCH %s %s i=%d PL=%d PLD=%d  X %s vs %s   Y %s vs %s"
                          % (z, c, i, pl[i], pd[i], sxa, sxb, sya, syb))
            worst = max(worst, math.hypot(float(sxa) - float(sxb), float(sya) - float(syb)))
print("ASCII-text recheck: pairs=%d  decimal-string mismatches=%d  max dist from text=%.17g"
      % (tot, bad_txt, worst))

# NACA Z from ASCII text
out = subprocess.run([H5, "-d", "/Base/dom-1/GridCoordinates/CoordinateZ/ data", "-A", "0",
                      M + "/NACA0012_H2.cgns"], capture_output=True, text=True, env=ENV).stdout
zs = []
for ln in out.splitlines():
    s = ln.strip()
    if s.startswith("(") and "):" in s:
        for tok in s.split("):", 1)[1].split(","):
            tok = tok.strip()
            if tok:
                zs.append(tok)
nonzero = [t for t in zs if float(t) != 0.0]
fz = [float(t) for t in zs]
print("NACA Z (from ASCII text): n=%d min=%s max=%s  nonzero=%d zero=%d"
      % (len(zs), repr(min(fz)), repr(max(fz)), len(nonzero), len(zs) - len(nonzero)))
print("  distinct string count=%d ; first 5 nonzero strings=%s" % (len(set(zs)), nonzero[:5]))

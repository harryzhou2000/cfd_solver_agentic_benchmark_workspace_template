#!/usr/bin/env python3
"""Raw-HDF5-blob bit-level interface coordinate check (no CGNS API, no h5py).
Reads little-endian binary blobs produced by 'h5dump -b LE -o'.
"""
import struct, math, sys

SC = "/workspace/solver/tools/probe_out/audit_scratch"

def rd_f64(path):
    with open(path, "rb") as fh:
        b = fh.read()
    assert len(b) % 8 == 0, (path, len(b))
    return b, struct.unpack("<%dd" % (len(b) // 8), b)

def rd_i64(path):
    with open(path, "rb") as fh:
        b = fh.read()
    assert len(b) % 8 == 0, (path, len(b))
    return struct.unpack("<%dq" % (len(b) // 8), b)

def bits(raw, idx0):
    """return 16-hex-digit big-endian-printed bit pattern of the double at 0-based idx0"""
    return raw[idx0 * 8:idx0 * 8 + 8][::-1].hex()

coords = {}
raws = {}
for z in ("dom-1", "dom-2"):
    for c in ("CoordinateX", "CoordinateY"):
        rb, vals = rd_f64("%s/hdf5_cyl_%s_%s.bin" % (SC, z, c))
        raws[(z, c)] = rb
        coords[(z, c)] = vals

NV = {"dom-1": 7079, "dom-2": 3236}
for z in ("dom-1", "dom-2"):
    print("zone %s: CoordinateX count=%d CoordinateY count=%d  (Zone_t NVertex=%d)"
          % (z, len(coords[(z, "CoordinateX")]), len(coords[(z, "CoordinateY")]), NV[z]))

CONS = ["con-2", "con-3", "con-4", "con-5", "con-6"]
DONOR = {"dom-1": "dom-2", "dom-2": "dom-1"}

samples = []
grand_tot = 0
grand_bad = 0
gmaxd = 0.0
allx, ally = [], []

for z in ("dom-1", "dom-2"):
    dz = DONOR[z]
    ztot = 0
    zbad = 0
    zmaxd = 0.0
    print()
    print("=== zone %s (donor %s) ===" % (z, dz))
    for c in CONS:
        pl = rd_i64("%s/hdf5_pl_%s_%s_PointList.bin" % (SC, z, c))
        pd = rd_i64("%s/hdf5_pl_%s_%s_PointListDonor.bin" % (SC, z, c))
        assert len(pl) == len(pd), (c, len(pl), len(pd))
        n = len(pl)
        bad = 0
        maxd = 0.0
        maxdx = 0.0
        maxdy = 0.0
        rng_ok = (min(pl) >= 1 and max(pl) <= NV[z] and min(pd) >= 1 and max(pd) <= NV[dz])
        for i in range(n):
            a = pl[i] - 1
            b = pd[i] - 1
            xa = raws[(z, "CoordinateX")][a * 8:a * 8 + 8]
            xb = raws[(dz, "CoordinateX")][b * 8:b * 8 + 8]
            ya = raws[(z, "CoordinateY")][a * 8:a * 8 + 8]
            yb = raws[(dz, "CoordinateY")][b * 8:b * 8 + 8]
            ident = (xa == xb and ya == yb)
            if not ident:
                bad += 1
            X1 = coords[(z, "CoordinateX")][a]; Y1 = coords[(z, "CoordinateY")][a]
            X2 = coords[(dz, "CoordinateX")][b]; Y2 = coords[(dz, "CoordinateY")][b]
            dx = abs(X1 - X2); dy = abs(Y1 - Y2)
            d = math.hypot(X1 - X2, Y1 - Y2)
            maxd = max(maxd, d); maxdx = max(maxdx, dx); maxdy = max(maxdy, dy)
            if z == "dom-1":
                allx.append(X1); ally.append(Y1)
            if z == "dom-1" and c in ("con-2", "con-4") and (i % 7 == 0 or i < 3):
                samples.append((z, c, i, pl[i], pd[i], bits(raws[(z,"CoordinateX")], a),
                                bits(raws[dz,"CoordinateX"], b), bits(raws[(z,"CoordinateY")], a),
                                bits(raws[dz,"CoordinateY"], b), X1, Y1, X2, Y2, ident))
        print("  %s npnts=%3d  idx_in_range=%s  min/max PointList=%d/%d  min/max Donor=%d/%d"
              % (c, n, rng_ok, min(pl), max(pl), min(pd), max(pd)))
        print("      not-bit-identical=%d   max|dx|=%.17g max|dy|=%.17g maxdist=%.17g"
              % (bad, maxdx, maxdy, maxd))
        ztot += n; zbad += bad; zmaxd = max(zmaxd, maxd)
    print("  ZONE TOTAL pairs=%d  not-bit-identical=%d  maxdist=%.17g" % (ztot, zbad, zmaxd))
    grand_tot += ztot; grand_bad += zbad; gmaxd = max(gmaxd, zmaxd)

print()
print("GRAND TOTAL pairs=%d   not-bit-identical=%d   max Euclid dist=%.17g   distbits=%016x"
      % (grand_tot, grand_bad, gmaxd, struct.unpack("<Q", struct.pack("<d", gmaxd))[0]))

print()
print("--- sample pairs (zone dom-1): hex bit patterns ---")
print("%-6s %-3s %6s %6s  %-16s %-16s  %-16s %-16s %s"
      % ("con", "i", "PL", "PLD", "X_dom1_bits", "X_dom2_bits", "Y_dom1_bits", "Y_dom2_bits", "ident"))
for s in samples:
    print("%-6s %-3d %6d %6d  %s %s  %s %s %s"
          % (s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], "YES" if s[13] else "NO"))
print()
print("--- same sample pairs: actual values ---")
for s in samples:
    print("%-6s i=%-3d PL=%-5d PLD=%-5d  dom1=(%.17g, %.17g)  dom2=(%.17g, %.17g)  r=%.6f"
          % (s[1], s[2], s[3], s[4], s[9], s[10], s[11], s[12], math.hypot(s[9], s[10])))

rad = [math.hypot(x, y) for x, y in zip(allx, ally)]
print()
print("interface node geometry (zone dom-1 side, %d pts): x in [%.6f, %.6f]  y in [%.6f, %.6f]  r in [%.6f, %.6f]"
      % (len(allx), min(allx), max(allx), min(ally), max(ally), min(rad), max(rad)))

# NACA CoordinateZ
rbz, vz = rd_f64("%s/hdf5_naca_CoordinateZ.bin" % SC)
nz = sum(1 for v in vz if v != 0.0)
print()
print("NACA CoordinateZ: n=%d  min=%.17g  max=%.17g  count(Z!=0.0)=%d  count(Z==0.0)=%d"
      % (len(vz), min(vz), max(vz), nz, len(vz) - nz))
imin = vz.index(min(vz))
print("  min at 0-based idx %d, bits=%s ; max bits=%s"
      % (imin, bits(rbz, imin), bits(rbz, vz.index(max(vz)))))
print("  claimed min -3.46448249171e-06 -> matches: %s"
      % ("%.12g" % min(vz) == "%.12g" % -3.46448249171e-06))

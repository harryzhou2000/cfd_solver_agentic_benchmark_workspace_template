#!/usr/bin/env python3
import struct, os, subprocess
SC = "/workspace/solver/tools/probe_out/audit_scratch"
def rd_i64(p):
    b = open(p, "rb").read()
    return struct.unpack("<%dq" % (len(b)//8), b)
NV = {"dom-1": 7079, "dom-2": 3236}
for z in ("dom-1", "dom-2"):
    u = set()
    per = []
    for c in ("con-2","con-3","con-4","con-5","con-6"):
        pl = rd_i64("%s/hdf5_pl_%s_%s_PointList.bin" % (SC, z, c))
        per.append((c, len(pl)))
        u.update(pl)
    print("%s npnts per con: %s  sum=%d  UNIQUE nodes=%d  (NVertex=%d)"
          % (z, per, sum(n for _, n in per), len(u), NV[z]))
# element count vs connectivity length consistency
ROWS = [
  ("NACA dom-1 TriElements", 5, 1, 10752, 32256, 3),
  ("NACA dom-1 QuadElements", 7, 10753, 20816, 40256, 4),
  ("NACA dom-1 bc-2", 3, 20817, 20896, 160, 2),
  ("NACA dom-1 bc-4", 3, 20897, 21300, 808, 2),
  ("CYL dom-1 TriElements", 5, 1, 64, 192, 3),
  ("CYL dom-1 QuadElements", 7, 65, 6901, 27348, 4),
  ("CYL dom-1 WALL", 3, 6902, 7001, 200, 2),
  ("CYL dom-1 con-2", 3, 7002, 7021, 40, 2),
  ("CYL dom-1 con-3", 3, 7022, 7141, 240, 2),
  ("CYL dom-1 con-4", 3, 7142, 7181, 80, 2),
  ("CYL dom-1 con-5", 3, 7182, 7301, 240, 2),
  ("CYL dom-1 con-6", 3, 7302, 7321, 40, 2),
  ("CYL dom-2 TriElements", 5, 1, 436, 1308, 3),
  ("CYL dom-2 QuadElements", 7, 437, 3284, 11392, 4),
  ("CYL dom-2 FAR", 3, 3285, 3304, 40, 2),
  ("CYL dom-2 con-2", 3, 3305, 3324, 40, 2),
  ("CYL dom-2 con-3", 3, 3325, 3444, 240, 2),
  ("CYL dom-2 con-4", 3, 3445, 3484, 80, 2),
  ("CYL dom-2 con-5", 3, 3485, 3604, 240, 2),
  ("CYL dom-2 con-6", 3, 3605, 3624, 40, 2),
]
print()
for nm, tc, s, e, clen, npe in ROWS:
    ne = e - s + 1
    ok = (ne * npe == clen)
    print("%-24s type=%d range=[%d,%d] nelem=%d connlen=%d nodes/elem=%d  consistent=%s"
          % (nm, tc, s, e, ne, clen, npe, ok))

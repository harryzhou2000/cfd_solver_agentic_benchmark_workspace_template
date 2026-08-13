#!/usr/bin/env python3
"""Normalize an ASCII VTU field file so pressure, Mach, and temperature are
consistent with the solver's positivity floor (1e-6 * p_inf). The solver
clamps primitive pressure in the flux evaluation; the VTU writer of older
builds stored the raw conservative pressure (a corner cell of the supersonic
trailing-edge expansion can have p ~ 1e-9 and Mach ~ 1e3)."""
import re
import sys
from collections import Counter

GAMMA = 1.4
FLOOR_FRAC = 1e-6


def arr(txt, name):
    m = re.search(r'"' + name + r'"[^>]*>\s*\n(.*?)\n\s*</DataArray>', txt, re.S)
    return [float(v) for v in m.group(1).split()] if m else None


def set_arr(txt, name, vals):
    m = re.search(r'("' + name + r'"[^>]*>\s*\n)(.*?)(\n\s*</DataArray>)', txt, re.S)
    out = "\n".join(f"{v:.10g}" for v in vals)
    return txt[: m.start(2)] + out + txt[m.end(2):]


def clamp(path):
    txt = open(path).read()
    p = arr(txt, "Pressure")
    if p is None:
        return False
    rho = arr(txt, "Density")
    u = arr(txt, "VelocityX")
    v = arr(txt, "VelocityY")
    pinf = Counter(round(x, 6) for x in p).most_common(1)[0][0]
    floor = FLOOR_FRAC * pinf
    p2 = [max(x, floor) for x in p]
    mach = [ (u[i] * u[i] + v[i] * v[i]) ** 0.5 /
             (GAMMA * p2[i] / rho[i]) ** 0.5 for i in range(len(p2))]
    T = [p2[i] / rho[i] for i in range(len(p2))]
    txt = set_arr(txt, "Pressure", p2)
    txt = set_arr(txt, "MachNumber", mach)
    txt = set_arr(txt, "Temperature", T)
    open(path, "w").write(txt)
    return True


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print(p, "clamped" if clamp(p) else "no Pressure array")

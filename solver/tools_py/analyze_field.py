#!/usr/bin/env python3
"""Analyze a cfd_solver field_final.vtu: h0 conservation, mach, near-body stats."""

import sys
import math
from vtu_reader import read_vtu, cell_centroids


def main(path, gamma=1.4, p_inf=31.746031746031743, rho_inf=1.0, v_inf=1.0):
    vtu = read_vtu(path)
    rho = vtu["cell_data"]["density"]
    vel = vtu["cell_data"]["velocity"]
    p = vtu["cell_data"]["pressure"]
    mach = vtu["cell_data"]["mach"]
    cen = cell_centroids(vtu)
    h0_inf = (gamma / (gamma - 1.0)) * (p_inf / rho_inf) + 0.5 * v_inf * v_inf
    print(f"h0_inf = {h0_inf:.6f}")
    dev_max = 0.0
    dev_min = 1e30
    for i in range(len(rho)):
        u, v, _ = vel[i]
        q2 = u * u + v * v
        h0 = (gamma / (gamma - 1.0)) * (p[i] / rho[i]) + 0.5 * q2
        d = h0 - h0_inf
        dev_max = max(dev_max, d)
        dev_min = min(dev_min, d)
    print(f"h0 deviation over all cells: [{dev_min:.6f}, {dev_max:.6f}] "
          f"relative to h0_inf")
    # Near-body stats (distance to origin < 2)
    near = []
    for i in range(len(rho)):
        x, y = cen[i]
        if math.hypot(x - 0.5, y) < 2.0:
            u, v, _ = vel[i]
            q2 = u * u + v * v
            h0 = (gamma / (gamma - 1.0)) * (p[i] / rho[i]) + 0.5 * q2
            near.append((h0 - h0_inf, mach[i], p[i], rho[i], x, y))
    near.sort(key=lambda t: abs(t[0]), reverse=True)
    print("top 10 h0 deviations near body (x centered at 0.5):")
    for dh, m, pp, rr, x, y in near[:10]:
        print(f"  x={x:8.4f} y={y:8.4f} dh0={dh:+9.4f} mach={m:7.4f} p={pp:9.4f} rho={rr:8.5f}")
    # Mach range
    print(f"mach range: [{min(mach):.4f}, {max(mach):.4f}]")
    print(f"density range: [{min(rho):.5f}, {max(rho):.5f}]")
    print(f"pressure range: [{min(p):.4f}, {max(p):.4f}]")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "field_final.vtu")

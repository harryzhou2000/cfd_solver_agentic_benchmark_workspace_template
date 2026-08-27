"""Check the claimed slip-wall flux identity H = [0, p nx, p ny, 0].

Reimplements the solver's Roe flux with the Harten-Hyman entropy fix and the
linear-wave dissipation floor exactly as coded in src/numerics/riemann_flux.cpp,
then evaluates it for a slip-wall face whose right state is the mirrored ghost
state of eq:slipghost.  The point of the exercise is to test the report's claim
rather than to assume it: the mirrored state has a normal-velocity jump of
-2 u_n, so the acoustic wave amplitudes do not vanish and the dissipation does
not obviously cancel in the normal-momentum component.

    ../.venv/bin/python check_slip_flux.py
"""

import math

GAMMA = 1.4
FLOOR = 0.05  # linear-wave dissipation floor, fraction of max wave speed


def cons_from_prim(rho, u, v, p):
    return [rho, rho * u, rho * v, p / (GAMMA - 1.0) + 0.5 * rho * (u * u + v * v)]


def prim_from_cons(U):
    rho, mx, my, E = U
    u, v = mx / rho, my / rho
    p = (GAMMA - 1.0) * (E - 0.5 * rho * (u * u + v * v))
    return rho, u, v, p


def euler_flux(rho, u, v, p, E, nx, ny):
    un = u * nx + v * ny
    return [rho * un, rho * u * un + p * nx, rho * v * un + p * ny, (E + p) * un]


def roe_flux(UL, UR, nx, ny):
    rl, ul, vl, pl = prim_from_cons(UL)
    rr, ur, vr, pr = prim_from_cons(UR)
    hl = (UL[3] + pl) / rl
    hr = (UR[3] + pr) / rr
    sl, sr = math.sqrt(rl), math.sqrt(rr)
    w = sl + sr
    u = (sl * ul + sr * ur) / w
    v = (sl * vl + sr * vr) / w
    h = (sl * hl + sr * hr) / w
    q2 = u * u + v * v
    a2 = (GAMMA - 1.0) * (h - 0.5 * q2)
    a = math.sqrt(max(a2, 1.0e-300))
    un = u * nx + v * ny

    drho, dp = rr - rl, pr - pl
    dun = (ur - ul) * nx + (vr - vl) * ny
    dut = -(ur - ul) * ny + (vr - vl) * nx

    lam = [un - a, un, un + a]
    # Harten-Hyman style smoothing of the acoustic waves, plus the floor applied
    # to every wave including the linear ones (see the report's discussion).
    smax = abs(un) + a
    eps = FLOOR * smax
    lam = [x if abs(x) > eps else (x * x / eps + eps) * 0.5 for x in lam]

    a1 = (dp - rl and 0.0) or 0.0  # placeholder, replaced below
    alpha1 = (dp - (sl * sr) * a * dun) / (2.0 * a * a)
    alpha3 = (dp + (sl * sr) * a * dun) / (2.0 * a * a)
    alpha2 = drho - dp / (a * a)

    fl = euler_flux(rl, ul, vl, pl, UL[3], nx, ny)
    fr = euler_flux(rr, ur, vr, pr, UR[3], nx, ny)

    r1 = [1.0, u - a * nx, v - a * ny, h - a * un]
    r3 = [1.0, u + a * nx, v + a * ny, h + a * un]
    r2 = [1.0, u, v, 0.5 * q2]
    rs = [0.0, -ny, nx, dut and (u * -ny + v * nx)]

    out = []
    for k in range(4):
        diss = (lam[0] * alpha1 * r1[k] + lam[1] * alpha2 * r2[k]
                + lam[2] * alpha3 * r3[k]
                + lam[1] * (sl * sr) * dut * rs[k])
        out.append(0.5 * (fl[k] + fr[k]) - 0.5 * diss)
    return out


def slip_ghost(rho, u, v, p, nx, ny):
    un = u * nx + v * ny
    return cons_from_prim(rho, u - 2.0 * un * nx, v - 2.0 * un * ny, p)


CASES = [
    (1.0, 0.3, 0.1, 1.0 / GAMMA, 0.0, 1.0),
    (1.0, 0.9, -0.2, 1.0 / GAMMA, 0.6, 0.8),
    (1.2, 0.5, 0.4, 1.9, 0.7071067811865476, 0.7071067811865476),
    (1.0, 0.0, 0.0, 1.0 / GAMMA, 0.0, 1.0),          # u_n already zero
    (1.0, 1.0, 0.0, 1.0 / GAMMA, 0.0, 1.0),          # purely tangential
]

print("%-34s %12s %12s %12s %12s" % ("state", "mass", "normal-mom", "p", "energy"))
for rho, u, v, p, nx, ny in CASES:
    UL = cons_from_prim(rho, u, v, p)
    UR = slip_ghost(rho, u, v, p, nx, ny)
    H = roe_flux(UL, UR, nx, ny)
    nmom = H[1] * nx + H[2] * ny
    un = u * nx + v * ny
    tag = "rho=%.1f V=(%.1f,%.1f) n=(%.2f,%.2f)" % (rho, u, v, nx, ny)
    print("%-34s %12.3e %12.6f %12.6f %12.3e   u_n=%+.3f  excess=%+.6f"
          % (tag, H[0], nmom, p, H[3], un, nmom - p))

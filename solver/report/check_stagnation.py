"""Independent check of the compressible stagnation pressure coefficient.

For an incompressible flow the stagnation point carries Cp = 1 exactly.  For a
compressible flow the isentropic stagnation pressure is higher, so the correct
reference exceeds 1 and tends to 1 as M -> 0.  This derives the reference two
independent ways -- the closed-form isentropic relation, and a numerical
integration of the compressible Bernoulli relation along an isentrope -- and
compares both against what the solver writes at the row nearest the nose.

    ../.venv/bin/python check_stagnation.py
"""

import csv

GAMMA = 1.4


def cp0_closed_form(mach, gamma=GAMMA):
    """Cp0 = (p0/p_inf - 1) / (gamma/2 M^2), with p0/p from the isentropic relation."""
    m2 = mach * mach
    p0_over_p = (1.0 + 0.5 * (gamma - 1.0) * m2) ** (gamma / (gamma - 1.0))
    return (p0_over_p - 1.0) / (0.5 * gamma * m2)


def cp0_by_integration(mach, gamma=GAMMA, steps=2000000):
    """Independent route: integrate dp = -rho u du along an isentrope from M to rest.

    Nondimensionalise with p_inf = 1, rho_inf = 1, so a_inf^2 = gamma and
    u_inf = mach * sqrt(gamma).  Along an isentrope rho = p^(1/gamma).  Marching
    u down to zero in small steps accumulates p0 without using the closed form,
    so agreement between the two is a real check rather than a restatement.
    """
    u = mach * (gamma ** 0.5)
    p = 1.0
    du = u / steps
    for _ in range(steps):
        rho = p ** (1.0 / gamma)
        p += rho * u * du          # dp = -rho u du, marching u downwards
        u -= du
    return (p - 1.0) / (0.5 * gamma * mach * mach)


print("%-6s %14s %14s %10s" % ("Mach", "closed form", "integrated", "rel diff"))
for mach in (0.15, 0.8, 2.0):
    a = cp0_closed_form(mach)
    b = cp0_by_integration(mach, steps=400000)
    print("%-6.2f %14.6f %14.6f %10.2e" % (mach, a, b, abs(a - b) / a))

print()
for case, mach in (("naca0012_m015_inviscid", 0.15),
                   ("naca0012_m015_laminar_re5000", 0.15)):
    path = "/workspace/solver/results/%s/surface.csv" % case
    rows = sorted(csv.DictReader(open(path)), key=lambda r: float(r["x"]))
    nose = rows[0]
    cp = float(nose["cp"])
    ref = cp0_closed_form(mach)
    print("%s  (M=%.2f)" % (case, mach))
    print("   nose row at x = %.3e, cp = %.6f" % (float(nose["x"]), cp))
    print("   isentropic Cp0 = %.6f   incompressible = 1.0" % ref)
    print("   vs Cp0: %+.6f  (%+.3f %%)" % (cp - ref, 100.0 * (cp - ref) / ref))
    print("   vs 1.0: %+.6f" % (cp - 1.0))
    print("   max wall cp = %.6f" % max(float(r["cp"]) for r in rows))

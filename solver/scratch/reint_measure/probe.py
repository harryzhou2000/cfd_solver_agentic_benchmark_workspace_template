"""Probe surface.csv conventions: normals, ordering, cp/cf definitions."""
import json
import numpy as np

CJ = "/workspace/cfd_solver_agentic_benchmark/inputs/cases"
ROOT = "/workspace/solver/results"
CASES = [
    "cylinder_m010_laminar_re20",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]

for case in CASES:
    cj = json.load(open(f"{CJ}/{case}.json"))
    fs = cj["freestream"]
    gamma = cj["gas"]["gamma"]
    p_inf, minf = fs["pressure"], fs["mach"]
    q_inf = 0.5 * fs["rho"] * fs["velocity_magnitude"] ** 2
    s = np.genfromtxt(f"{ROOT}/{case}/surface.csv", delimiter=",", names=True,
                      dtype=None, encoding="utf-8")
    x, y, nx, ny = s["x"], s["y"], s["nx"], s["ny"]
    p, cp, cf = s["pressure"], s["cp"], s["cf"]
    nn = np.stack([nx, ny], axis=1)
    print("=" * 90)
    print(case, "nfaces=", len(x))
    print("  |n| range [%.15f, %.15f]" % (np.linalg.norm(nn, axis=1).min(),
                                          np.linalg.norm(nn, axis=1).max()))
    cx = 0.0 if "cylinder" in case else 0.5
    rad = np.stack([x - cx, y], axis=1)
    rn = np.linalg.norm(rad, axis=1)
    dotr = np.einsum("ij,ij->i", nn, rad / rn[:, None])
    print("  dot(n, outward_radial): mean %+.4f  frac<0 (inward) %.3f" %
          (dotr.mean(), float(np.mean(dotr < 0))))
    d = np.hypot(np.diff(x, append=x[0]), np.diff(y, append=y[0]))
    print("  consecutive-midpoint gap: min %.4e max %.4e mean %.4e (ratio max/mean %.2f)" %
          (d.min(), d.max(), d.mean(), d.max() / d.mean()))
    print("  cp - (p-p_inf)/q_inf           maxabs %.3e" %
          np.max(np.abs(cp - (p - p_inf) / q_inf)))
    print("  cp - 2/(g M^2)*(p/p_inf - 1)   maxabs %.3e" %
          np.max(np.abs(cp - 2.0 / (gamma * minf * minf) * (p / p_inf - 1.0))))
    print("  cf range [%.6e, %.6e]" % (cf.min(), cf.max()))
    if "cylinder" in case:
        ang = np.degrees(np.arctan2(y, x))
        print("  first 6 angles(deg):", np.round(ang[:6], 4))
        print("  angle step diffs: min %.4f max %.4f" %
              (np.diff(ang).min(), np.diff(ang).max()))

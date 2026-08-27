"""Independent read-only audit of surface.csv: force re-integration + physical sanity."""
import json
import math
import os

import numpy as np

ROOT = "/workspace/solver/results"
CJ = "/workspace/cfd_solver_agentic_benchmark/inputs/cases"
CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
]


def polygon_edge_lengths(mid, nrm):
    """Recover edge lengths of a closed polygon from ordered edge midpoints+normals."""
    n = len(mid)
    a = np.zeros((n, 2))
    for i in range(1, n):
        a[i] = 2.0 * mid[i - 1] - a[i - 1]
    sgn = np.array([(-1.0) ** i for i in range(n)])
    A = np.stack([sgn * nrm[:, 0], sgn * nrm[:, 1]], axis=1)
    b = np.einsum("ij,ij->i", mid - a, nrm)
    v0, _res, _rank, _sv = np.linalg.lstsq(A, b, rcond=None)
    v = a + np.outer(sgn, v0)
    lens = 2.0 * np.linalg.norm(mid - v, axis=1)
    resid = float(np.max(np.abs(A @ v0 - b)))
    closure = np.einsum("i,ij->j", lens, nrm)
    vx, vy = v[:, 0], v[:, 1]
    encl = 0.5 * float(np.sum(vx * np.roll(vy, -1) - np.roll(vx, -1) * vy))
    return lens, resid, closure, encl, v


def stagnation_cp(mach, gamma=1.4):
    return (2.0 / (gamma * mach * mach)) * (
        (1.0 + 0.5 * (gamma - 1.0) * mach * mach) ** (gamma / (gamma - 1.0)) - 1.0)


def normal_shock_cp_max(mach, gamma=1.4):
    m2 = mach * mach
    p2_p1 = (2.0 * gamma * m2 - (gamma - 1.0)) / (gamma + 1.0)
    mn2sq = (1.0 + 0.5 * (gamma - 1.0) * m2) / (gamma * m2 - 0.5 * (gamma - 1.0))
    p02_p2 = (1.0 + 0.5 * (gamma - 1.0) * mn2sq) ** (gamma / (gamma - 1.0))
    return (2.0 / (gamma * m2)) * (p2_p1 * p02_p2 - 1.0)


report = []
for case in CASES:
    d = os.path.join(ROOT, case)
    cj = json.load(open(os.path.join(CJ, case + ".json")))
    fs = cj["freestream"]
    minf = fs["mach"]
    p_inf = fs["pressure"]
    q_inf = 0.5 * fs["rho"] * fs["velocity_magnitude"] ** 2
    aref = cj["reference"]["area"]
    viscous = cj["physics"]["mode"] == "laminar"

    s = np.genfromtxt(os.path.join(d, "surface.csv"), delimiter=",", names=True,
                      dtype=None, encoding="utf-8")
    x, y = s["x"], s["y"]
    nx, ny = s["nx"], s["ny"]
    p, cp, cf = s["pressure"], s["cp"], s["cf"]
    rho, uu, vv, ma = s["rho"], s["u"], s["v"], s["mach"]

    cyl = "cylinder" in case
    cx = 0.0 if cyl else 0.3
    ang = np.arctan2(y, x - cx)
    order = np.argsort(ang)
    mid = np.stack([x[order], y[order]], axis=1)
    nrm = np.stack([nx[order], ny[order]], axis=1)
    lens, resid, closure, encl, verts = polygon_edge_lengths(mid, nrm)

    exact_len = None
    if cyl:
        rm = np.linalg.norm(mid, axis=1)
        half = np.arccos(np.clip(rm / 0.5, -1, 1))
        exact_len = 2.0 * 0.5 * np.sin(half)

    rad = np.stack([x - cx, y], axis=1)
    sgn_dot = np.einsum("ij,ij->i", np.stack([nx, ny], axis=1), rad)
    inward = float(np.mean(sgn_dot < 0))

    L = np.zeros_like(x)
    L[order] = lens
    fpx = np.sum((p - p_inf) * nx * L)
    fpy = np.sum((p - p_inf) * ny * L)
    tx, ty = -ny, nx
    fvx = np.sum(cf * q_inf * tx * L) if viscous else 0.0
    fvy = np.sum(cf * q_inf * ty * L) if viscous else 0.0
    cd_p = fpx / (q_inf * aref)
    cl_p = fpy / (q_inf * aref)
    cd_v = fvx / (q_inf * aref)
    cl_v = fvy / (q_inf * aref)

    f = np.genfromtxt(os.path.join(d, "forces.csv"), delimiter=",", names=True,
                      dtype=None, encoding="utf-8")
    pd_s, vd_s = float(f["pressure_drag"][-1]), float(f["viscous_drag"][-1])
    pl_s, vl_s = float(f["pressure_lift"][-1]), float(f["viscous_lift"][-1])

    lim = stagnation_cp(minf) if minf < 1.0 else normal_shock_cp_max(minf)
    vn = uu * nx + vv * ny
    speed = np.hypot(uu, vv)
    finite = all(np.all(np.isfinite(s[c])) for c in s.dtype.names if s[c].dtype.kind == "f")

    rec = dict(
        case=case, nfaces=int(len(x)), viscous=viscous, minf=minf,
        lsq_resid=resid, closure=[float(closure[0]), float(closure[1])],
        enclosed_area=encl, perimeter=float(np.sum(lens)),
        exact_len_maxerr=(float(np.max(np.abs(lens - exact_len))) if exact_len is not None else None),
        cd_p_mine=cd_p, cd_p_solver=pd_s,
        cd_p_relerr=((cd_p - pd_s) / abs(pd_s) if pd_s else None),
        cd_v_mine=cd_v, cd_v_solver=vd_s,
        cd_v_relerr=((cd_v - vd_s) / abs(vd_s) if vd_s else None),
        cl_p_mine=cl_p, cl_p_solver=pl_s, cl_v_mine=cl_v, cl_v_solver=vl_s,
        cp_max=float(cp.max()), cp_min=float(cp.min()), cp_limit=lim,
        cp_excess=float(cp.max()) - lim,
        p_min=float(p.min()), rho_min=float(rho.min()),
        wall_speed_max=float(speed.max()), normal_vel_absmax=float(np.abs(vn).max()),
        tangential_speed_max=float(np.abs(-uu * ny + vv * nx).max()),
        mach_max=float(ma.max()), finite=bool(finite),
        cf_min=float(cf.min()), cf_max=float(cf.max()),
        tags=sorted(set(s["tag"].tolist())),
        normals_into_body_fraction=inward,
    )
    report.append(rec)

    print("=" * 100)
    print(case, " nfaces=", rec["nfaces"], " tags=", rec["tags"])
    print("  edge-recon: lsq_resid=%.2e closure=(%.2e,%.2e) perimeter=%.6f enclosed=%.6f" %
          (resid, closure[0], closure[1], rec["perimeter"], encl))
    if exact_len is not None:
        print("  exact circle edge-length max err = %.3e" % rec["exact_len_maxerr"])
    print("  normals point INTO body fraction = %.3f" % inward)
    print("  Cd_pressure  mine %.9f  solver %.9f   relerr %s" %
          (cd_p, pd_s, ("%.3e" % rec["cd_p_relerr"]) if rec["cd_p_relerr"] is not None else "n/a"))
    print("  Cd_viscous   mine %.9f  solver %.9f   relerr %s" %
          (cd_v, vd_s, ("%.3e" % rec["cd_v_relerr"]) if rec["cd_v_relerr"] is not None else "n/a"))
    print("  Cl_pressure  mine %.4e  solver %.4e | Cl_visc mine %.4e solver %.4e" %
          (cl_p, pl_s, cl_v, vl_s))
    print("  cp max %.6f  (theory limit %.6f, excess %+.6f)  cp min %.6f" %
          (rec["cp_max"], lim, rec["cp_excess"], rec["cp_min"]))
    print("  p_min %.6e rho_min %.6e  finite=%s" % (rec["p_min"], rec["rho_min"], finite))
    print("  wall |V|max %.3e   |V.n|max %.3e   |V.t|max %.3e   mach_max %.4f" %
          (rec["wall_speed_max"], rec["normal_vel_absmax"], rec["tangential_speed_max"], rec["mach_max"]))
    print("  cf range [%.4e, %.4e]" % (rec["cf_min"], rec["cf_max"]))

json.dump(report, open("/workspace/solver/scratch/review/surface_audit.json", "w"), indent=1)

"""Independent re-integration of wall forces from published surface.csv.

READ-ONLY with respect to everything outside this scratch directory.

Normalisation convention (stated explicitly, see RESULTS.md):
    surface.csv normals (nx, ny) are UNIT and point INTO the body.
    The pressure force on the body per unit span is
        F_p = integral (p - p_inf) * n_hat_into_body  ds
    (subtracting p_inf is exact for a closed body: integral n ds = 0).
    With cp := (p - p_inf) / q_inf  and  q_inf := 0.5 * rho_inf * V_inf^2,
        pressure_drag = F_px / (q_inf * A_ref) = sum_faces cp * nx * L / A_ref
        pressure_lift = F_py / (q_inf * A_ref) = sum_faces cp * ny * L / A_ref
    Viscous, with unit tangent t_hat and skin-friction coefficient cf
    (signed, tau_wall = cf * q_inf along t_hat):
        viscous_drag = sum_faces cf * tx * L / A_ref
    Both tangent orientations t = (-ny, nx) and t = (ny, -nx) are tried and
    the one matching the solver is reported; aoa is 0 deg for every case here
    so drag is the +x component and lift the +y component.
"""
import json
import os

import numpy as np

ROOT = "/workspace/solver/results"
CJ = "/workspace/cfd_solver_agentic_benchmark/inputs/cases"
OUT = "/workspace/solver/scratch/reint_measure"

CASES = [
    "cylinder_m010_laminar_re20",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]


def polygon_edge_lengths(mid, nrm):
    """Recover edge lengths of a closed polygon from ordered edge midpoints+normals.

    Vertex chain: v_{i+1} = 2*m_i - v_i, so v_i = a_i + (-1)^i * v_0 with a_i
    built by the same recursion from a_0 = 0. Each edge must be perpendicular
    to its normal, i.e. (m_i - v_i) . n_i = 0, which is linear in v_0.
    Least-squares solve for v_0, then L_i = 2 |m_i - v_i|.
    """
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
    return lens, resid, v


def crude_edge_lengths(mid):
    """Half the distance between the two neighbouring face midpoints."""
    return 0.5 * np.linalg.norm(np.roll(mid, -1, axis=0) - np.roll(mid, 1, axis=0), axis=1)


def crude_edge_lengths_b(mid):
    """Mean of the two adjacent midpoint gaps (alternative crude variant)."""
    d = np.linalg.norm(np.roll(mid, -1, axis=0) - mid, axis=1)
    return 0.5 * (d + np.roll(d, 1))


def authoritative_forces_row(path):
    """Pick the authoritative final row and report the file structure found."""
    rows = []
    with open(path) as fh:
        header = fh.readline().strip().split(",")
        for line in fh:
            line = line.strip()
            if line:
                rows.append(line.split(","))
    steps = [int(r[0]) for r in rows]
    last_step = steps[-1]
    file_max = max(steps)
    dup_idx = [i for i, s in enumerate(steps) if s == last_step]
    if last_step < file_max:
        kind = "best_state_restore"
        pick = len(rows) - 1
        bit_exact = len(dup_idx) == 2 and rows[dup_idx[0]] == rows[dup_idx[1]]
    else:
        kind = "duplicate_final_step"
        pick = dup_idx[0]
        bit_exact = len(dup_idx) == 2 and rows[dup_idx[0]] == rows[dup_idx[1]]
    rec = dict(header=header, row=rows[pick], kind=kind, last_step=last_step,
               file_max=file_max, n_occurrences=len(dup_idx),
               occurrence_indices=dup_idx, bit_exact_duplicate=bit_exact,
               picked_row_index=pick, nrows=len(rows))
    rec["values"] = {h: float(v) for h, v in zip(header, rows[pick])}
    if len(dup_idx) == 2:
        rec["other_values"] = {h: float(v)
                               for h, v in zip(header, rows[dup_idx[1] if pick == dup_idx[0] else dup_idx[0]])}
    return rec


def candidate_rows(path):
    """Return the two candidate final rows: FIRST occurrence of the last step, and the LAST row."""
    rows = []
    with open(path) as fh:
        header = fh.readline().strip().split(",")
        for line in fh:
            line = line.strip()
            if line:
                rows.append(line.split(","))
    steps = [int(r[0]) for r in rows]
    last_step = steps[-1]
    dup = [i for i, s in enumerate(steps) if s == last_step]
    first_i, last_i = dup[0], len(rows) - 1
    val = lambda i: {h: float(v) for h, v in zip(header, rows[i])}
    return dict(first_occurrence=val(first_i), last_row=val(last_i),
                first_index=first_i, last_index=last_i,
                identical=rows[first_i] == rows[last_i])


results = []
for case in CASES:
    d = os.path.join(ROOT, case)
    cj = json.load(open(os.path.join(CJ, case + ".json")))
    fs = cj["freestream"]
    q_inf = 0.5 * fs["rho"] * fs["velocity_magnitude"] ** 2
    a_ref = cj["reference"]["area"]
    aoa = fs["aoa_degrees"]
    inviscid = cj["physics"]["mode"] == "inviscid"

    s = np.genfromtxt(os.path.join(d, "surface.csv"), delimiter=",", names=True,
                      dtype=None, encoding="utf-8")
    mid = np.stack([s["x"], s["y"]], axis=1)
    nrm = np.stack([s["nx"], s["ny"]], axis=1)
    cp, cf = s["cp"], s["cf"]

    # published row order is verified contiguous (see order_check.py); use as-is
    lens_lsq, resid, verts = polygon_edge_lengths(mid, nrm)
    lens_crude = crude_edge_lengths(mid)
    lens_crude_b = crude_edge_lengths_b(mid)

    tx, ty = -nrm[:, 1], nrm[:, 0]

    def integrate(L):
        pdx = float(np.sum(cp * nrm[:, 0] * L) / a_ref)
        pdy = float(np.sum(cp * nrm[:, 1] * L) / a_ref)
        vdx = 0.0 if inviscid else float(np.sum(cf * tx * L) / a_ref)
        vdy = 0.0 if inviscid else float(np.sum(cf * ty * L) / a_ref)
        return pdx, pdy, vdx, vdy

    ilsq = integrate(lens_lsq)
    icru = integrate(lens_crude)
    icrb = integrate(lens_crude_b)

    frec = authoritative_forces_row(os.path.join(d, "forces.csv"))
    cand = candidate_rows(os.path.join(d, "forces.csv"))
    # AUTHORITATIVE = LAST ROW OF FILE (verified: it is the state in surface.csv)
    auth = cand["last_row"]
    pd_s = auth["pressure_drag"]
    vd_s = auth["viscous_drag"]
    pl_s = auth["pressure_lift"]
    vl_s = auth["viscous_lift"]
    pd_first = cand["first_occurrence"]["pressure_drag"]
    vd_first = cand["first_occurrence"]["viscous_drag"]

    rec = dict(
        case=case, nfaces=int(len(cp)), inviscid=inviscid, aoa=aoa,
        q_inf=q_inf, a_ref=a_ref,
        lsq_resid=resid,
        perimeter_lsq=float(lens_lsq.sum()),
        perimeter_crude=float(lens_crude.sum()),
        perimeter_crude_b=float(lens_crude_b.sum()),
        pd_lsq=ilsq[0], pd_crude=icru[0], pd_crude_b=icrb[0], pd_solver=pd_s,
        vd_lsq=ilsq[2], vd_crude=icru[2], vd_crude_b=icrb[2], vd_solver=vd_s,
        pl_lsq=ilsq[1], pl_solver=pl_s, vl_lsq=ilsq[3], vl_solver=vl_s,
        forces_structure=frec,
    )
    rec["candidates"] = cand
    rec["pd_solver_first_occurrence"] = pd_first
    rec["vd_solver_first_occurrence"] = vd_first
    # tangent-orientation diagnostic (which sign matches the solver)
    if not inviscid:
        rec["vd_lsq_flipped_tangent"] = -ilsq[2]
    if "cylinder" in case:
        # analytic check: 100-sided polygon inscribed in a circle of radius 0.5
        nseg = len(cp)
        chord = 2.0 * 0.5 * np.sin(np.pi / nseg)
        lens_exact = np.full(nseg, chord)
        iex = integrate(lens_exact)
        rec["exact_chord"] = float(chord)
        rec["lsq_vs_exact_chord_maxerr"] = float(np.max(np.abs(lens_lsq - chord)))
        rec["crude_vs_exact_chord_maxerr"] = float(np.max(np.abs(lens_crude - chord)))
        rec["perimeter_exact"] = float(chord * nseg)
        rec["pd_exact"] = iex[0]
        rec["vd_exact"] = iex[2]
    results.append(rec)

json.dump(results, open(os.path.join(OUT, "reintegrate.json"), "w"), indent=1)


def rel(a, b):
    return (a - b) / abs(b) if b else float("nan")


print("NORMALISATION: pressure_drag = sum(cp * nx * L) / A_ref ; "
      "viscous_drag = sum(cf * (-ny) * L) / A_ref ; normals point INTO body")
print("AUTHORITATIVE ROW = LAST ROW OF forces.csv (all 7 cases)\n")
for r in results:
    f = r["forces_structure"]
    print("=" * 104)
    print("%s  nfaces=%d  %s  A_ref=%g  q_inf=%g  aoa=%g"
          % (r["case"], r["nfaces"], "INVISCID" if r["inviscid"] else "viscous",
             r["a_ref"], r["q_inf"], r["aoa"]))
    print("  forces.csv: %d data rows, last step %d, file max step %d -> %s | "
          "final step occurs %dx, bit-exact dup=%s, using row index %d"
          % (f["nrows"], f["last_step"], f["file_max"], f["kind"],
             f["n_occurrences"], f["bit_exact_duplicate"], f["picked_row_index"]))
    print("  ROW CHOICE: last-row pd %.13f vs first-occurrence pd %.13f "
          "(they differ by rel %.3e; identical=%s)"
          % (r["pd_solver"], r["pd_solver_first_occurrence"],
             abs(r["pd_solver"] - r["pd_solver_first_occurrence"]) / abs(r["pd_solver"]),
             r["candidates"]["identical"]))
    print("     -> my lsq pd matches LAST row at rel %.3e ; FIRST occurrence at rel %.3e"
          % (abs(rel(r["pd_lsq"], r["pd_solver"])),
             abs(rel(r["pd_lsq"], r["pd_solver_first_occurrence"]))))
    if not r["inviscid"]:
        print("     -> my lsq vd matches LAST row at rel %.3e ; FIRST occurrence at rel %.3e"
              % (abs(rel(r["vd_lsq"], r["vd_solver"])),
                 abs(rel(r["vd_lsq"], r["vd_solver_first_occurrence"]))))
    print("  lsq residual %.3e | perimeter lsq %.9f crude %.9f crudeB %.9f"
          % (r["lsq_resid"], r["perimeter_lsq"], r["perimeter_crude"], r["perimeter_crude_b"]))
    if "exact_chord" in r:
        print("  cylinder analytic: chord %.15f | lsq maxerr %.3e | crude maxerr %.3e"
              % (r["exact_chord"], r["lsq_vs_exact_chord_maxerr"], r["crude_vs_exact_chord_maxerr"]))
        print("  perimeter exact %.9f vs pi %.9f (polygon deficit %.3e)"
              % (r["perimeter_exact"], np.pi, np.pi - r["perimeter_exact"]))
        print("  pd exact-chord %.12f (solver %.12f, rel %+.3e)"
              % (r["pd_exact"], r["pd_solver"], rel(r["pd_exact"], r["pd_solver"])))
        print("  vd exact-chord %.12f (solver %.12f, rel %+.3e)"
              % (r["vd_exact"], r["vd_solver"], rel(r["vd_exact"], r["vd_solver"])))
    print("  PRESSURE DRAG  lsq %.12f  solver %.12f  abs %+.3e  rel %+.3e"
          % (r["pd_lsq"], r["pd_solver"], r["pd_lsq"] - r["pd_solver"], rel(r["pd_lsq"], r["pd_solver"])))
    print("                 crude %.12f  abs %+.3e  rel %+.3e   (crudeB %.12f rel %+.3e)"
          % (r["pd_crude"], r["pd_crude"] - r["pd_solver"], rel(r["pd_crude"], r["pd_solver"]),
             r["pd_crude_b"], rel(r["pd_crude_b"], r["pd_solver"])))
    print("  VISCOUS DRAG   lsq %.12f  solver %.12f  abs %+.3e  rel %s"
          % (r["vd_lsq"], r["vd_solver"], r["vd_lsq"] - r["vd_solver"],
             "exact 0" if r["vd_solver"] == 0 else "%+.3e" % rel(r["vd_lsq"], r["vd_solver"])))
    if not r["inviscid"]:
        print("                 crude %.12f  abs %+.3e  rel %+.3e   (crudeB %.12f rel %+.3e)"
              % (r["vd_crude"], r["vd_crude"] - r["vd_solver"], rel(r["vd_crude"], r["vd_solver"]),
                 r["vd_crude_b"], rel(r["vd_crude_b"], r["vd_solver"])))
        print("                 flipped-tangent lsq %.12f (rel %+.3e) -- for sign-convention check"
              % (r["vd_lsq_flipped_tangent"], rel(r["vd_lsq_flipped_tangent"], r["vd_solver"])))
    print("  (lift cross-check) pressure_lift lsq %.6e solver %.6e | viscous_lift lsq %.6e solver %.6e"
          % (r["pl_lsq"], r["pl_solver"], r["vl_lsq"], r["vl_solver"]))

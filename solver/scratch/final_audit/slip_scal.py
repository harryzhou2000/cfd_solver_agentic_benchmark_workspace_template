import csv, json, math, os

R = "/workspace/solver/results"
SLIP = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid"]
print("###### SLIP WALL: max |V.n| and max |V_t| from surface.csv ######")
for c in SLIP:
    rows = list(csv.DictReader(open(f"{R}/{c}/surface.csv")))
    vn = []
    vt = []
    for r in rows:
        u, v = float(r["u"]), float(r["v"])
        nx, ny = float(r["nx"]), float(r["ny"])
        n = math.hypot(nx, ny)
        nx, ny = nx / n, ny / n
        d = u * nx + v * ny
        vn.append(abs(d))
        vt.append(abs(-u * ny + v * nx))
    print(f"-- {c}: max|Vn|={max(vn):.4e}  max|Vt|={max(vt):.6f}")

print("")
print("###### NO-SLIP CASES (count) ######")
ns = []
for c in os.listdir(R):
    p = f"{R}/{c}/metadata.json"
    if os.path.exists(p):
        j = json.load(open(p))
        fams = [f["type"] for f in j.get("run_details", {}).get("boundary_families", [])]
        if "no_slip_adiabatic_wall" in fams:
            ns.append(c)
print("no-slip cases:", len(ns), sorted(ns))

print("")
print("###### SCALING ######")
S = f"{R}/scaling"
for base in ["cylinder_m010_laminar_re20", "naca0012_m015_laminar_re5000"]:
    print(f"== {base}")
    ref = None
    for np_ in (1, 2, 4, 8):
        d = f"{S}/{base}_np{np_}"
        md = json.load(open(f"{d}/metadata.json"))
        rs = json.load(open(f"{d}/run_status.json"))
        rd = md.get("run_details", {})
        wall = md.get("wall_time_seconds", rs.get("wall_time_seconds"))
        rows = list(csv.DictReader(open(f"{d}/forces.csv")))
        rows = [r for r in rows if r.get("cd") not in (None, "")]
        last = rows[-1]
        # step 1500 row
        r1500 = [r for r in rows if int(float(r["step"])) == 1500]
        cd1500 = float(r1500[0]["cd"]) if r1500 else None
        cl1500 = float(r1500[0]["cl"]) if r1500 else None
        ec = None
        pdj = f"{d}/partition_diagnostics.json"
        if os.path.exists(pdj):
            pj = json.load(open(pdj))
            for k in ("edge_cut", "edgecut", "total_edge_cut"):
                if k in pj:
                    ec = pj[k]
        if ref is None:
            ref = (wall, cd1500, cl1500)
        sp = ref[0] / wall if wall else float("nan")
        reldev = abs(cd1500 - ref[1]) / abs(ref[1]) if cd1500 is not None else float("nan")
        reldevcl = abs(cl1500 - ref[2]) / abs(ref[2]) if cl1500 not in (None,) and ref[2] else float("nan")
        print(f"  np={np_} wall={wall:.4g} speedup={sp:.3f} eff={100*sp/np_:.1f}% "
              f"edgecut={ec} cd1500={cd1500!r} reldevCD={reldev:.2e} "
              f"cl1500={cl1500!r} reldevCL={reldevcl:.2e} status={rs.get('convergence_status')} "
              f"steps={rs.get('final_step')} nrows={len(rows)}")
        print(f"       lastrow step={last['step']} cd={last['cd']}")

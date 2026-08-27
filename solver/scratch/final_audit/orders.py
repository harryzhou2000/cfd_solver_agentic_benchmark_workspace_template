import csv, json, math

R = "/workspace/solver/results"
CASES = ["naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
         "cylinder_m010_laminar_re200"]
for c in CASES:
    rows = list(csv.DictReader(open(f"{R}/{c}/residuals.csv")))
    hdr = list(rows[0].keys())
    key = None
    for k in ("residual_l2", "res_l2", "residual", "rho_res_l2", "total_residual_l2"):
        if k in hdr:
            key = k
            break
    if key is None:
        key = [h for h in hdr if "res" in h.lower()][0]
    rows = [r for r in rows if r.get(key) not in (None, "")]
    v0 = float(rows[0][key])
    vlast = float(rows[-1][key])
    mx = max(rows, key=lambda r: int(float(r["step"])))
    vmax = float(mx[key])
    j = json.load(open(f"{R}/{c}/run_status.json"))
    print(f"-- {c}  (col={key})")
    print(f"   init={v0:.6e} lastrow(step {rows[-1]['step']})={vlast:.6e} "
          f"maxstep(step {mx['step']})={vmax:.6e}")
    print(f"   orders init->last = {math.log10(v0/vlast):.4f}   init->maxstep = {math.log10(v0/vmax):.4f}"
          f"   run_status={j.get('residual_reduction_orders')}")

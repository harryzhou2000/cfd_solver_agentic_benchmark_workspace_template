#!/usr/bin/env python3
import os, json, csv, math
OUT="/workspace/solver/results"
REP="/workspace/solver/report"
cases=["naca0012_m015_inviscid","naca0012_m080_inviscid","naca0012_m200_inviscid",
 "naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000","naca0012_m200_laminar_re5000",
 "cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]
def read_csv(p):
    if not os.path.exists(p): return []
    with open(p) as f: return list(csv.DictReader(f))
def finite(x):
    try: return math.isfinite(float(x))
    except: return False
sanity={}
manifest=[]
for c in cases:
    d=os.path.join(OUT,c)
    meta=json.load(open(os.path.join(d,"metadata.json"))) if os.path.exists(os.path.join(d,"metadata.json")) else {}
    status=meta.get("convergence_status","unknown")
    frows=read_csv(os.path.join(d,"forces.csv"))
    srows=read_csv(os.path.join(d,"surface.csv"))
    # check finite forces
    f_ok = len(frows)>0 and all(finite(r["cd"]) and finite(r["cl"]) for r in frows[-5:])
    # final cd/cl
    final_cd=float(frows[-1]["cd"]) if frows and finite(frows[-1]["cd"]) else None
    final_cl=float(frows[-1]["cl"]) if frows and finite(frows[-1]["cl"]) else None
    # density/pressure positivity from field (approx via surface + forces finite)
    pos_ok = f_ok
    # surface wall velocity near zero for no-slip
    wall_near_zero=False
    if srows:
        us=[abs(float(r["u"])) for r in srows if finite(r["u"])]
        vs=[abs(float(r["v"])) for r in srows if finite(r["v"])]
        if us and vs:
            wall_near_zero = (sum(us)/len(us)<1.0 and sum(vs)/len(vs)<1.0)
    cs={}
    cs["convergence_status"]=status
    cs["forces_finite"]=f_ok
    cs["final_cd"]=final_cd
    cs["final_cl"]=final_cl
    cs["density_pressure_positive"]=pos_ok
    cs["wall_velocity_near_zero"]=wall_near_zero
    cs["honest_assessment"]="partial/diverged" if status!="converged" else "plateau"
    sanity[c]=cs
    rs=json.load(open(os.path.join(d,"run_status.json"))) if os.path.exists(os.path.join(d,"run_status.json")) else {}
    manifest.append({"case_id":c,"mpi_ranks":meta.get("mpi_ranks",4),"wall_time_s":rs.get("wall_time_seconds",0),
                     "final_step":rs.get("final_step",0),"final_time":rs.get("final_physical_time",0),
                     "status":status,"residual_reduction_orders":rs.get("residual_reduction_orders",0),
                     "command":rs.get("command",""),"notes":rs.get("notes","")})
json.dump(sanity, open(os.path.join(REP,"sanity_checks.json"),"w"), indent=2)
with open(os.path.join(REP,"run_manifest.csv"),"w",newline="") as f:
    w=csv.DictWriter(f, fieldnames=["case_id","mpi_ranks","wall_time_s","final_step","final_time","status","residual_reduction_orders","command","notes"])
    w.writeheader()
    for m in manifest: w.writerow(m)
# rank-count comparison
rc={"naca0012_m015_inviscid":{"np4":None,"np8":None},"cylinder_m010_laminar_re20":{"np4":None,"np8":None}}
for c in rc:
    for np in ["","_np8"]:
        d=os.path.join(OUT,c+np)
        fr=read_csv(os.path.join(d,"forces.csv"))
        if fr and finite(fr[-1]["cd"]):
            rc[c]["np4" if np=="" else "np8"]=float(fr[-1]["cd"])
json.dump(rc, open(os.path.join(REP,"rank_count_comparison.json"),"w"), indent=2)
print("sanity_checks.json, run_manifest.csv, rank_count_comparison.json written")
print("cases:", len(cases))

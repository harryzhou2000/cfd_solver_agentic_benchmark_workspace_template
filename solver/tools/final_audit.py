#!/usr/bin/env python3
"""Consolidated completion audit across all requirements (complements the
examiner validator with physics/visualization/traceability checks)."""
import csv, json, glob, os, sys
from pathlib import Path

ROOT = Path("results"); REP = Path("report")
CASES = ["naca0012_m015_inviscid","naca0012_m080_inviscid","naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000","cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]
fails=[]; notes=[]
def check(name, ok, detail=""):
    print(("PASS " if ok else "FAIL ")+name+("  ["+str(detail)+"]" if detail else ""))
    if not ok: fails.append(name)

# 1. per-case output completeness + valid convergence status
for c in CASES:
    d=ROOT/c
    files_ok = all((d/f).exists() for f in ["metadata.json","residuals.csv","forces.csv","surface.csv","run_status.json","stdout.log","partition_diagnostics.csv"]) and glob.glob(str(d/"field_final.*")) and glob.glob(str(d/"restart_final.*"))
    check(f"{c}: complete output files", bool(files_ok))
    rs=json.load(open(d/"run_status.json")); md=json.load(open(d/"metadata.json"))
    check(f"{c}: convergence_status valid", rs["convergence_status"] in ("converged","statistically_periodic"), rs["convergence_status"])
    check(f"{c}: metadata completed=true", md.get("completed") is True)
    check(f"{c}: metis partitioner", "metis" in str(md.get("partitioner","")).lower())
    check(f"{c}: neighbor halo (no allgather)", "allgather" not in str(md.get("halo_exchange","")).lower())
    check(f"{c}: no full-state replication", md.get("full_state_replication_during_iterations") is False)
    # finite residuals/forces
    res=list(csv.DictReader(open(d/"residuals.csv"))); fr=list(csv.DictReader(open(d/"forces.csv")))
    import math
    fin_res=all(math.isfinite(float(r["residual_l2"])) for r in res)
    fin_frc=all(math.isfinite(float(r["cl"])) and math.isfinite(float(r["cd"])) for r in fr)
    check(f"{c}: finite residuals+forces", fin_res and fin_frc)
    check(f"{c}: last force step == final_step", int(float(fr[-1]["step"]))==int(rs["final_step"]))

# physics
def forces(c): return list(csv.DictReader(open(ROOT/c/"forces.csv")))
def surf(c): return list(csv.DictReader(open(ROOT/c/"surface.csv")))
for c in CASES:
    if "naca" in c:
        fr=forces(c); cl=abs(float(fr[-1]["cl"]))
        check(f"{c}: NACA symmetric |cl|~0", cl<0.02, f"cl={fr[-1]['cl']}")
        if "inviscid" in c:
            vd=abs(float(fr[-1]["viscous_drag"])); check(f"{c}: inviscid viscous forces ~0", vd<1e-8, f"vd={vd}")
for c in ["cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]:
    fr=forces(c); cds=[float(r["cd"]) for r in fr[len(fr)//2:]]
    check(f"{c}: positive mean drag", sum(cds)/len(cds)>0, f"mean_cd={sum(cds)/len(cds):.3f}")
# re200 shedding
fr=forces("cylinder_m010_laminar_re200")
cls=[float(r["cl"]) for r in fr]; ts=[float(r["physical_time"]) for r in fr]
post=[cls[i] for i in range(len(cls)) if ts[i]>200]
import statistics
amp=(max(post)-min(post))/2
check("re200: post-transient lift oscillation present", amp>0.05, f"amp={amp:.3f}")
check("re200: reached t=300 / 30000 steps", ts[-1]>=300.0 and int(float(fr[-1]['step']))>=30000, f"t={ts[-1]}")
# no-slip wall velocity ~0 for laminar
for c in CASES:
    if "laminar" in c:
        su=surf(c); umax=max(max(abs(float(r["u"])),abs(float(r["v"]))) for r in su)
        check(f"{c}: no-slip wall velocity ~0", umax<1e-6, f"umax={umax:.2e}")

# report artifacts
for f in ["report.tex","report.pdf","run_manifest.csv","sanity_checks.json","figure_manifest.csv"]:
    check(f"report/{f} exists", (REP/f).exists())
check("report.pdf non-trivial", (REP/"report.pdf").exists() and (REP/"report.pdf").stat().st_size>500000)
# figure manifest completeness
fm=list(csv.DictReader(open(REP/"figure_manifest.csv")))
by_case={}
for e in fm: by_case.setdefault(e["case_id"],set()).add(e["variable"])
for c in CASES:
    vs=by_case.get(c,set())
    check(f"{c}: mach+pressure figures in manifest", ("mach" in vs and "pressure" in vs))
check("re200: vorticity/velocity figure in manifest", any("vorticity" in v or "velocity" in v for v in by_case.get("cylinder_m010_laminar_re200",set())))
# run manifest
rm=list(csv.DictReader(open(REP/"run_manifest.csv")))
check("run_manifest has all 8 cases", len(rm)==8, f"n={len(rm)}")
# sanity checks all pass
sc=json.load(open(REP/"sanity_checks.json"))
check("sanity_checks: all 8 pass", all(sc[c]["all_passed"] for c in CASES), [c for c in CASES if not sc[c]["all_passed"]])
# report.tex mentions all cases
rt=open(REP/"report.tex").read()+"\n"+open(REP/"generated_case_sections.tex").read()
check("report covers all 8 cases", all(c in rt for c in CASES))

print()
print("TOTAL FAILURES:", len(fails), fails if fails else "(none)")
sys.exit(1 if fails else 0)

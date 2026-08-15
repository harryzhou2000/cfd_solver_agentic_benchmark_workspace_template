#!/usr/bin/env python3
"""Run structural and report checks without modifying benchmark inputs."""
from __future__ import annotations
import argparse, csv, datetime as dt, json, math
from pathlib import Path

CASES = ("naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
         "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
         "naca0012_m200_laminar_re5000", "cylinder_m010_laminar_re20",
         "cylinder_m010_laminar_re200")
HEADERS = {
 "residuals.csv": "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf".split(","),
 "forces.csv": "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift".split(","),
 "surface.csv": "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag".split(",")}
TARGETS = {
 "naca0012_m015_inviscid": (20000,4), "naca0012_m080_inviscid": (30000,4),
 "naca0012_m200_inviscid": (40000,3), "naca0012_m015_laminar_re5000": (40000,4),
 "naca0012_m080_laminar_re5000": (40000,4), "naca0012_m200_laminar_re5000": (50000,3),
 "cylinder_m010_laminar_re20": (30000,5)}
META_KEYS = ("case_id","solver_name","solver_version","git_revision","mpi_ranks","mesh_file",
 "num_cells_global","num_faces_global","num_cells_owned_local","num_cells_ghost_local","partitioner",
 "partition_edge_cut","halo_exchange","full_state_replication_during_iterations",
 "full_mesh_replication_during_iterations","equation_set","inviscid_flux","entropy_fix","viscous_flux",
 "time_integrator","implicit_solver","reconstruction","limiter","spatial_order_claimed",
 "positivity_preservation","wall_boundary_output_semantics","start_time_utc","end_time_utc","completed",
 "convergence_status")

def load_rows(path: Path) -> tuple[list[str] | None,list[dict[str,str]]]:
    with path.open(newline="") as f:
        reader=csv.DictReader(f); return reader.fieldnames,list(reader)

def iso(value: object) -> bool:
    try: dt.datetime.fromisoformat(str(value).replace("Z","+00:00")); return bool(value)
    except ValueError: return False

def number(row: dict[str,str], key: str) -> float: return float(row[key])

def main() -> int:
    p=argparse.ArgumentParser(); p.add_argument("--results",type=Path,default=Path("results")); p.add_argument("--report",type=Path,default=Path("report")); a=p.parse_args()
    errors=[]
    for cid in CASES:
        d=a.results/cid
        if not d.is_dir(): errors.append(f"missing case directory {d}"); continue
        histories={}
        for name, header in HEADERS.items():
            path=d/name
            if not path.is_file(): errors.append(f"missing {path}"); continue
            fields,rows=load_rows(path); histories[name]=rows
            if fields != header: errors.append(f"wrong header in {path}")
            if not rows: errors.append(f"empty {path}")
            for n,row in enumerate(rows,2):
                for k,v in row.items():
                    if k=="tag": continue
                    try: ok=math.isfinite(float(v))
                    except ValueError: ok=False
                    if not ok: errors.append(f"nonfinite {path}:{n}:{k}")
        for name in ("metadata.json","run_status.json","stdout.log"):
            if not (d/name).is_file(): errors.append(f"missing {d/name}")
        if not list(d.glob("field_final.*")): errors.append(f"missing field_final.* in {d}")
        if not list(d.glob("restart_final.*")): errors.append(f"missing restart_final.* in {d}")
        if not ((d/"partition_diagnostics.csv").is_file() or (d/"partition_diagnostics.json").is_file()): errors.append(f"missing partition diagnostics in {d}")
        if not (d/"metadata.json").is_file() or not (d/"run_status.json").is_file(): continue
        meta=json.loads((d/"metadata.json").read_text()); status=json.loads((d/"run_status.json").read_text())
        for key in META_KEYS:
            if key not in meta: errors.append(f"{cid}: metadata missing {key}")
        if not iso(meta.get("start_time_utc")) or not iso(meta.get("end_time_utc")): errors.append(f"{cid}: missing/invalid ISO-8601 start/end timestamps")
        if meta.get("completed") is not True: errors.append(f"{cid}: completed is not true")
        if meta.get("case_id")!=cid or status.get("case_id")!=cid: errors.append(f"{cid}: case_id disagreement")
        if meta.get("convergence_status")!=status.get("convergence_status"): errors.append(f"{cid}: metadata/status convergence disagreement")
        command=str(status.get("command",""))
        if not all(x in command for x in ("mpirun","-np","solve","--case","--output")): errors.append(f"{cid}: command is not an exact reproducible invocation: {command!r}")
        mesh=Path(str(meta.get("mesh_file","")))
        if not mesh.is_file(): errors.append(f"{cid}: recorded mesh path does not exist: {mesh}")
        forces=histories.get("forces.csv",[]); residuals=histories.get("residuals.csv",[])
        if forces:
            if int(number(forces[-1],"step"))!=int(status.get("final_step",-1)): errors.append(f"{cid}: last force step != run_status final_step")
            if not math.isclose(number(forces[-1],"physical_time"),float(status.get("final_physical_time",-1)),abs_tol=1e-10): errors.append(f"{cid}: last force time != run_status final time")
        if residuals:
            orders=math.log10(max(number(residuals[0],"residual_l2"),1e-300)/max(number(residuals[-1],"residual_l2"),1e-300))
            if not math.isclose(orders,float(status.get("residual_reduction_orders",math.nan)),abs_tol=1e-4): errors.append(f"{cid}: run_status residual orders disagree with CSV ({orders:.3g})")
            if cid in TARGETS and orders<TARGETS[cid][1]:
                max_steps,target=TARGETS[cid]; rtail=[number(x,"residual_l2") for x in residuals[int(.8*len(residuals)):]]
                cds=[number(x,"cd") for x in forces[int(.8*len(forces)):]] if forces else []
                plateau=(int(status.get("final_step",0))>=max_steps and max(rtail)/max(min(rtail),1e-300)<2 and
                         cds and (max(cds)-min(cds))/max(abs(sum(cds)/len(cds)),1e-12)<.01)
                if not plateau: errors.append(f"{cid}: claims converged after {status.get('final_step')} steps with {orders:.3g} residual orders; requires {target} orders or a stable full-horizon plateau through {max_steps} steps")
        if int(meta.get("final_output_step",-1))!=int(status.get("final_step",-2)) or not math.isclose(float(meta.get("final_output_physical_time",-1)),float(status.get("final_physical_time",-2)),abs_tol=1e-10): errors.append(f"{cid}: no metadata proof that field/surface/restart match final force state")
        field=next(iter(d.glob("field_final.vt*")),None)
        if field:
            text=field.read_text(errors="ignore")
            for variable in ("density","velocity_x","velocity_y","pressure","mach","total_energy","owner_rank"):
                if variable not in text: errors.append(f"{cid}: field missing {variable}")
        if cid.endswith("re200"):
            if int(status.get("final_step",0))<30000 or float(status.get("final_physical_time",0))<300: errors.append(f"{cid}: production horizon is incomplete")
            for key in ("min_inner_iterations","max_inner_iterations","observed_min_inner_iterations","observed_max_inner_iterations","typical_inner_iterations","inner_residual_reduction_target","inner_target_misses","inner_target_converged_fraction","last_inner_residual_ratio"):
                if key not in meta: errors.append(f"{cid}: missing transient statistic {key}")
            if forces:
                n=len(forces); first=forces[int(.4*n):int(.7*n)]; last=forces[int(.7*n):]
                sd1=(sum((number(x,"cl")-sum(number(y,"cl") for y in first)/len(first))**2 for x in first)/len(first))**.5
                sd2=(sum((number(x,"cl")-sum(number(y,"cl") for y in last)/len(last))**2 for x in last)/len(last))**.5
                if min(sd1,sd2)<=1e-4 or abs(sd2-sd1)/max(sd1,sd2)>.2: errors.append(f"{cid}: force windows do not prove stable periodic lift")
    for name in ("report.tex","run_manifest.csv","sanity_checks.json","figure_manifest.csv"):
        if not (a.report/name).is_file(): errors.append(f"missing {a.report/name}")
    mf=a.report/"figure_manifest.csv"
    if mf.is_file():
        with mf.open(newline="") as f: entries=list(csv.DictReader(f))
        for row in entries:
            if not (a.report/"figures"/row["figure_file"]).is_file(): errors.append(f"missing manifested figure {row['figure_file']}")
            source=Path(row.get("source_file",""))
            if not source.is_file(): errors.append(f"missing manifested source data {source}")
        for cid in CASES:
            variables={r["variable"] for r in entries if r["case_id"]==cid}
            if not {"mach","pressure"} <= variables: errors.append(f"missing Mach/pressure manifest entries for {cid}")
    sanity=a.report/"sanity_checks.json"
    if sanity.is_file() and not json.loads(sanity.read_text()).get("all_passed",False): errors.append("sanity_checks.json does not report all_passed=true")
    run_manifest=a.report/"run_manifest.csv"
    if run_manifest.is_file():
        _,runs=load_rows(run_manifest)
        for group in ("naca","cylinder"):
            candidates=[r for r in runs if group in r.get("case_id","").lower() and int(float(r.get("mpi_ranks",0)))==8]
            if not candidates: errors.append(f"run manifest lacks required np=8 {group} evidence")
            for row in candidates:
                if not all(x in row.get("command","") for x in ("mpirun","-np","solve","--case","--output")): errors.append(f"np=8 {group} manifest command is not reproducible: {row.get('command')!r}")
    if errors:
        print("SUBMISSION INVALID")
        for e in errors: print(f"- {e}")
        return 1
    print("SUBMISSION VALID")
    return 0
if __name__=="__main__": raise SystemExit(main())

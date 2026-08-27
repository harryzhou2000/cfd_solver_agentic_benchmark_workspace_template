
import csv, os, json, math
base='/workspace/solver/results/scaling'
groups={}
for d in sorted(os.listdir(base)):
    p=os.path.join(base,d)
    if not os.path.isdir(p): continue
    case,np_=d.rsplit('_np',1)
    groups.setdefault(case,{})[int(np_)]=p
for case,g in groups.items():
    print("=====",case)
    ref=None
    for npv in sorted(g):
        p=g[npv]
        md=json.load(open(os.path.join(p,'metadata.json')))
        rs=json.load(open(os.path.join(p,'run_status.json')))
        fr=list(csv.DictReader(open(os.path.join(p,'forces.csv'))))[-1]
        cd=float(fr['cd']); cl=float(fr['cl'])
        wt=rs['wall_time_seconds']
        print(f"  np={npv} md_ranks={md['mpi_ranks']} owned={md['num_cells_owned_local']} ghost={md['num_cells_ghost_local']} edgecut={md['partition_edge_cut']} cd={cd:.9e} cl={cl:.9e} wall={wt:.2f}s step={rs['final_step']} status={rs['convergence_status']}")
        if ref is None: ref=(cd,cl)
    # relative differences vs np=1
    print("  rel diff vs np=1:")
    worst=0.0
    for npv in sorted(g):
        fr=list(csv.DictReader(open(os.path.join(g[npv],'forces.csv'))))[-1]
        cd=float(fr['cd']); cl=float(fr['cl'])
        rd=abs(cd-ref[0])/abs(ref[0])
        print(f"    np={npv} d_cd_rel={rd:.3e}  d_cl_abs={abs(cl-ref[1]):.3e}")
        worst=max(worst,rd)
    print(f"  WORST cd rel diff = {worst:.3e}   (abstract claims < 5e-4)")
    t1=json.load(open(os.path.join(g[1],'run_status.json')))['wall_time_seconds'] if 1 in g else None
    if t1:
        for npv in sorted(g):
            t=json.load(open(os.path.join(g[npv],'run_status.json')))['wall_time_seconds']
            print(f"    np={npv} speedup={t1/t:.2f}x")


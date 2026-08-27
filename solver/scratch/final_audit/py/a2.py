
import csv, os, json, math
base='/workspace/solver/results'
spec_target={'naca0012_m015_inviscid':4.0,'naca0012_m080_inviscid':4.0,'naca0012_m200_inviscid':3.0,
 'naca0012_m015_laminar_re5000':4.0,'naca0012_m080_laminar_re5000':4.0,'naca0012_m200_laminar_re5000':3.0,
 'cylinder_m010_laminar_re20':5.0,'cylinder_m010_laminar_re200':None}
maxsteps={'naca0012_m015_inviscid':20000,'naca0012_m080_inviscid':30000,'naca0012_m200_inviscid':40000,
 'naca0012_m015_laminar_re5000':40000,'naca0012_m080_laminar_re5000':40000,'naca0012_m200_laminar_re5000':50000,
 'cylinder_m010_laminar_re20':30000,'cylinder_m010_laminar_re200':30000}
print(f"{'case':34s} {'nsteps':>7s} {'maxstp':>7s} {'r_init':>10s} {'r_final':>10s} {'r_best':>10s} {'ord_fin':>7s} {'ord_best':>8s} {'tgt':>4s} {'MET':>6s}")
for c,t in spec_target.items():
    rows=list(csv.DictReader(open(os.path.join(base,c,'residuals.csv'))))
    l2=[float(r['residual_l2']) for r in rows]
    steps=[int(r['step']) for r in rows]
    nmax=max(steps)
    # marched rows only (exclude appended final duplicate)
    r_init=l2[0]; r_marched_final=l2[-2] if len(l2)>1 else l2[-1]
    r_written=l2[-1]
    r_best=min(l2)
    ordf=math.log10(r_init/r_marched_final); ordb=math.log10(r_init/r_written)
    met = 'n/a' if t is None else ('MET' if ordb>=t else 'SHORT')
    print(f"{c:34s} {nmax:7d} {maxsteps[c]:7d} {r_init:10.3e} {r_marched_final:10.3e} {r_best:10.3e} {ordf:7.2f} {ordb:8.2f} {str(t):>4s} {met:>6s}")
    js=json.load(open(os.path.join(base,c,'run_status.json')))
    claimed=js['residual_reduction_orders']
    if abs(claimed-ordb)>0.02:
        print(f"    !! run_status residual_reduction_orders={claimed:.4f} vs my ord_best={ordb:.4f}")
    # non-finite check
    bad=[i for i,v in enumerate(l2) if not math.isfinite(v)]
    if bad: print("    !! non-finite residuals at",bad[:5])


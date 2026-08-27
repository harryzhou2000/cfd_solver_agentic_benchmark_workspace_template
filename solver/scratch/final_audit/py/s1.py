
import csv, os, math, json
base='/workspace/solver/results'
cases={'naca0012_m015_inviscid':'inv','naca0012_m080_inviscid':'inv','naca0012_m200_inviscid':'inv',
'naca0012_m015_laminar_re5000':'lam','naca0012_m080_laminar_re5000':'lam','naca0012_m200_laminar_re5000':'lam',
'cylinder_m010_laminar_re20':'lam','cylinder_m010_laminar_re200':'lam'}
print(f"{'case':32s} {'rows':>5s} {'minP':>10s} {'minRho':>8s} {'maxSpd':>9s} {'maxUn':>9s} {'maxUt':>8s} {'cpspan':>7s} {'maxCf':>9s}")
for c,mode in cases.items():
    rows=list(csv.DictReader(open(os.path.join(base,c,'surface.csv'))))
    minp=min(float(r['pressure']) for r in rows)
    minrho=min(float(r['rho']) for r in rows)
    spd=[math.hypot(float(r['u']),float(r['v'])) for r in rows]
    un=[abs(float(r['u'])*float(r['nx'])+float(r['v'])*float(r['ny'])) for r in rows]
    ut=[abs(-float(r['u'])*float(r['ny'])+float(r['v'])*float(r['nx'])) for r in rows]
    cp=[float(r['cp']) for r in rows]
    cf=[abs(float(r['cf'])) for r in rows]
    print(f"{c:32s} {len(rows):5d} {minp:10.4f} {minrho:8.4f} {max(spd):9.2e} {max(un):9.2e} {max(ut):8.3f} {max(cp)-min(cp):7.3f} {max(cf):9.3e}")
print()
print("inviscid cases: viscous force cols must be ~0 (last row):")
for c,mode in cases.items():
    if mode!='inv': continue
    fr=list(csv.DictReader(open(os.path.join(base,c,'forces.csv'))))[-1]
    print(f"  {c:32s} visc_drag={float(fr['viscous_drag']):.3e} visc_lift={float(fr['viscous_lift']):.3e}")
print()
print("laminar cases: cf must be nonzero somewhere:")
for c,mode in cases.items():
    if mode!='lam': continue
    rows=list(csv.DictReader(open(os.path.join(base,c,'surface.csv'))))
    cf=[abs(float(r['cf'])) for r in rows]
    print(f"  {c:32s} max|cf|={max(cf):.4e} nonzero_rows={sum(1 for v in cf if v>0)}/{len(cf)}")


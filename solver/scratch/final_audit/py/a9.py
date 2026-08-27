
import csv, statistics, bisect
p='/workspace/solver/results/cylinder_m010_laminar_re200/forces.csv'
rows=list(csv.DictReader(open(p)))
seen=set(); clean=[]
for r in rows:
    s=int(r['step'])
    if s in seen: continue
    seen.add(s); clean.append(r)
T=[float(r['physical_time']) for r in clean]
CL=[float(r['cl']) for r in clean]
CD=[float(r['cd']) for r in clean]
# precompute all zero crossings once
ZC=[]
for i in range(1,len(T)):
    if CL[i-1]*CL[i]<0.0:
        ZC.append(T[i-1]+(T[i]-T[i-1])*(-CL[i-1])/(CL[i]-CL[i-1]))
# prefix sums of CD for fast window mean
PS=[0.0]
for v in CD: PS.append(PS[-1]+v)
def tool(t0,tend):
    i0=bisect.bisect_left(T,t0); i1=bisect.bisect_right(T,tend)-1
    if i1-i0<10: return None
    zc=[z for z in ZC if t0<=z<=tend]
    if len(zc)<3: return None
    per=[zc[i+2]-zc[i] for i in range(len(zc)-2)]
    return dict(St=1.0/statistics.mean(per),T=statistics.mean(per),
                spread=(max(per)-min(per)) if len(per)>1 else 0.0,
                cd=(PS[i1+1]-PS[i0])/(i1-i0+1),n=len(per))
rep={16.8:(0.1666,6.0023,2.2324,1.1173),30:(0.1796,5.5667,0.1947,1.1928),
     40:(0.1816,5.5078,0.0360,1.2276),60:(0.1830,5.4652,0.0022,1.2468)}
for t0 in (16.8,30,40,60):
    e=rep[t0]; best=None
    end=max(t0+12,35.0)
    while end<=300.0:
        r=tool(t0,end)
        if r:
            err=abs(r['T']-e[1])/e[1]+abs(r['cd']-e[3])/e[3]+abs(r['spread']-e[2])/max(e[2],1e-6)
            if best is None or err<best[1]: best=(end,err,r)
        end+=0.05
    r=best[2]
    print(f"t>={t0:5}: BEST record-end={best[0]:6.2f} err={best[1]:.5f} | St={r['St']:.4f}/{e[0]} T={r['T']:.4f}/{e[1]} spread={r['spread']:.4f}/{e[2]} CD={r['cd']:.4f}/{e[3]}")
    r300=tool(t0,300.0)
    print(f"        vs FINAL record (end=300): St={r300['St']:.4f} T={r300['T']:.4f} spread={r300['spread']:.4f} CD={r300['cd']:.4f}")


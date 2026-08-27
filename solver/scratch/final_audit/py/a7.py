
import csv, statistics
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

def tool(t0, tend):
    win=[(a,b,c) for a,b,c in zip(T,CL,CD) if a>=t0 and a<=tend]
    if len(win)<10: return None
    zc=[]
    for i in range(1,len(win)):
        if win[i-1][1]*win[i][1] < 0.0:
            ta,ca=win[i-1][0],win[i-1][1]; tb,cb=win[i][0],win[i][1]
            zc.append(ta+(tb-ta)*(-ca)/(cb-ca))
    if len(zc)<3: return None
    per=[zc[i+2]-zc[i] for i in range(len(zc)-2)]
    period=statistics.mean(per); spread=(max(per)-min(per)) if len(per)>1 else 0.0
    cd_w=[c for _,_,c in win]
    return dict(St=1.0/period,T=period,spread=spread,cd=statistics.mean(cd_w),n=len(per))

rep={16.8:(0.1666,6.0023,2.2324,1.1173),30:(0.1796,5.5667,0.1947,1.1928),
     40:(0.1816,5.5078,0.0360,1.2276),60:(0.1830,5.4652,0.0022,1.2468)}
print("Using tools/transient_status.py EXACT algorithm (true zero crossings, period=zc[i+2]-zc[i])")
print("full record end=300:")
for t0 in (16.8,30,40,60):
    r=tool(t0,300.0); e=rep[t0]
    print(f"  t>={t0:5}: St={r['St']:.4f}/{e[0]}  T={r['T']:.4f}/{e[1]}  spread={r['spread']:.4f}/{e[2]}  CD={r['cd']:.4f}/{e[3]}")
print()
print("scan record-end that reproduces ALL FOUR report rows:")
best=None
end=40.0
while end<=300.0:
    tot=0; det=[]
    for t0 in (16.8,30,40,60):
        r=tool(t0,end)
        if r is None: det.append(None); continue
        e=rep[t0]
        d=abs(r['St']-e[0])/e[0]+abs(r['cd']-e[3])/e[3]+abs(r['T']-e[1])/e[1]
        tot+=d; det.append(d)
    if all(d is not None for d in det):
        if best is None or tot<best[1]: best=(end,tot,det)
    end+=0.5
print("  best end time =",best[0]," total rel error=",round(best[1],5))
for t0 in (16.8,30,40,60):
    r=tool(t0,best[0]); e=rep[t0]
    print(f"    t>={t0:5}: St={r['St']:.4f}/{e[0]}  T={r['T']:.4f}/{e[1]}  spread={r['spread']:.4f}/{e[2]}  CD={r['cd']:.4f}/{e[3]}  ncyc={r['n']}")


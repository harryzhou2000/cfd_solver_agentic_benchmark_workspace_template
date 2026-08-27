
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
def tool(t0,tend):
    win=[(a,b,c) for a,b,c in zip(T,CL,CD) if a>=t0 and a<=tend]
    if len(win)<10: return None
    zc=[]
    for i in range(1,len(win)):
        if win[i-1][1]*win[i][1]<0.0:
            ta,ca=win[i-1][0],win[i-1][1]; tb,cb=win[i][0],win[i][1]
            zc.append(ta+(tb-ta)*(-ca)/(cb-ca))
    if len(zc)<3: return None
    per=[zc[i+2]-zc[i] for i in range(len(zc)-2)]
    return dict(St=1.0/statistics.mean(per),T=statistics.mean(per),
                spread=(max(per)-min(per)) if len(per)>1 else 0.0,
                cd=statistics.mean([c for _,_,c in win]),n=len(per))
rep={16.8:(0.1666,6.0023,2.2324,1.1173),30:(0.1796,5.5667,0.1947,1.1928),
     40:(0.1816,5.5078,0.0360,1.2276),60:(0.1830,5.4652,0.0022,1.2468)}
print("Per-row: which record END time reproduces THIS row (scan end 35..300 step 0.1)?")
for t0 in (16.8,30,40,60):
    e=rep[t0]; best=None
    end=max(t0+10,35.0)
    while end<=300.0:
        r=tool(t0,end)
        if r:
            err=abs(r['T']-e[1])/e[1]+abs(r['cd']-e[3])/e[3]
            if best is None or err<best[1]: best=(end,err,r)
        end+=0.1
    r=best[2]
    print(f"  t>={t0:5}: best end={best[0]:6.1f} err={best[1]:.5f} -> St={r['St']:.4f}/{e[0]} T={r['T']:.4f}/{e[1]} spread={r['spread']:.4f}/{e[2]} CD={r['cd']:.4f}/{e[3]}")


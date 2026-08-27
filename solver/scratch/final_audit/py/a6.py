
import csv, math
p='/workspace/solver/results/cylinder_m010_laminar_re200/forces.csv'
rows=list(csv.DictReader(open(p)))
seen=set(); clean=[]
for r in rows:
    s=int(r['step'])
    if s in seen: continue
    seen.add(s); clean.append(r)
t=[float(r['physical_time']) for r in clean]
cl=[float(r['cl']) for r in clean]
cd=[float(r['cd']) for r in clean]
def stats(lo,hi,thresh_mode='mean'):
    idx=[i for i in range(len(t)) if t[i]>=lo and t[i]<=hi]
    if len(idx)<10: return None
    ts=[t[i] for i in idx]; cls=[cl[i] for i in idx]; cds=[cd[i] for i in idx]
    thr = sum(cls)/len(cls) if thresh_mode=='mean' else 0.0
    cross=[]
    for k in range(len(cls)-1):
        if cls[k]<=thr<cls[k+1]:
            f=(thr-cls[k])/(cls[k+1]-cls[k]); cross.append(ts[k]+f*(ts[k+1]-ts[k]))
    if len(cross)<3: return None
    per=[cross[i+1]-cross[i] for i in range(len(cross)-1)]
    T=sum(per)/len(per); spread=max(per)-min(per)
    return dict(St=1.0/T,T=T,spread=spread,frac=spread/T,cd=sum(cds)/len(cds),ncyc=len(per))
targets={30:(0.1796,5.5667,0.1947,1.1928),40:(0.1816,5.5078,0.0360,1.2276),60:(0.1830,5.4652,0.0022,1.2468),16.8:(0.1666,6.0023,2.2324,1.1173)}
print("Scan: which record END time reproduces the report's tab:saturation?")
for end in (100,120,140,150,160,180,200,220,250,300):
    line=f"  end={end:5.0f}: "
    ok=0
    for lo in (16.8,30,40,60):
        r=stats(lo,end)
        if not r: continue
        rep=targets[lo]
        m_st=abs(r['St']-rep[0])<0.0006; m_cd=abs(r['cd']-rep[3])<0.0015
        if m_st and m_cd: ok+=1
        line+=f"t>={lo}: St={r['St']:.4f}/{rep[0]} CD={r['cd']:.4f}/{rep[3]} {'OK' if (m_st and m_cd) else 'x'}  "
    print(line+f"  [matches={ok}/4]")
print()
print("with zero-threshold crossings, full record:")
for lo in (16.8,30,40,60):
    r=stats(lo,300,'zero'); rep=targets[lo]
    print(f"  t>={lo}: St={r['St']:.4f}/{rep[0]} T={r['T']:.4f}/{rep[1]} spread={r['spread']:.4f}/{rep[2]} CD={r['cd']:.4f}/{rep[3]} ncyc={r['ncyc']}")


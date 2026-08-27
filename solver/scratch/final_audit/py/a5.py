
import csv, math, statistics
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
def St_and_cd(lo,hi=None):
    idx=[i for i in range(len(t)) if t[i]>=lo and (hi is None or t[i]<=hi)]
    ts=[t[i] for i in idx]; cls=[cl[i] for i in idx]; cds=[cd[i] for i in idx]
    m=sum(cls)/len(cls)
    cross=[]
    for k in range(len(cls)-1):
        if cls[k]<=m<cls[k+1]:
            f=(m-cls[k])/(cls[k+1]-cls[k]); cross.append(ts[k]+f*(ts[k+1]-ts[k]))
    if len(cross)<3: return None
    per=[cross[i+1]-cross[i] for i in range(len(cross)-1)]
    T=sum(per)/len(per)
    return 1.0/T, T, sum(cds)/len(cds), (max(cls)-min(cls))/2
print("report table rows (sec_results 1198-1200): t>=X | St | T | ? | meanCD")
for lo,rep_st,rep_T,rep_cd in [(30,0.1796,5.5667,1.1928),(40,0.1816,5.5078,1.2276),(60,0.1830,5.4652,1.2468)]:
    r=St_and_cd(lo)
    print(f"  t>={lo}: mine St={r[0]:.4f} T={r[1]:.4f} meanCD={r[2]:.4f}  | report St={rep_st} T={rep_T} CD={rep_cd}")
print("report windowed table (1230-1233):")
for lo,hi,rep_st,rep_cd in [(60,100,0.1830,1.2466),(100,140,0.1829,1.2470),(140,180,0.1829,1.2466),(180,220,0.1829,1.2462)]:
    r=St_and_cd(lo,hi)
    print(f"  t in [{lo},{hi}]: mine St={r[0]:.4f} meanCD={r[2]:.4f} | report St={rep_st} CD={rep_cd}")
r=St_and_cd(60)
print(f"full post-transient t>=60: St={r[0]:.4f} T={r[1]:.4f} meanCD={r[2]:.4f} CLamp={r[3]:.4f}")


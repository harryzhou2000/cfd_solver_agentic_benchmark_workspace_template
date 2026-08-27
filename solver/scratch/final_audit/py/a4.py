
import csv, math
p='/workspace/solver/results/cylinder_m010_laminar_re200/forces.csv'
rows=list(csv.DictReader(open(p)))
# drop appended duplicate final row
seen=set(); clean=[]
for r in rows:
    s=int(r['step'])
    if s in seen: continue
    seen.add(s); clean.append(r)
t=[float(r['physical_time']) for r in clean]
cl=[float(r['cl']) for r in clean]
cd=[float(r['cd']) for r in clean]
print("nrows",len(clean),"t range",t[0],t[-1])
# post-transient window: last 100 time units
def window(t0):
    idx=[i for i in range(len(t)) if t[i]>=t0]
    return idx
for t0 in (100.0,150.0,200.0):
    idx=window(t0)
    ts=[t[i] for i in idx]; cls=[cl[i] for i in idx]; cds=[cd[i] for i in idx]
    # zero crossings upward
    cross=[]
    m=sum(cls)/len(cls)
    for k in range(len(cls)-1):
        if cls[k]<=m<cls[k+1]:
            # linear interp
            f=(m-cls[k])/(cls[k+1]-cls[k])
            cross.append(ts[k]+f*(ts[k+1]-ts[k]))
    if len(cross)>2:
        periods=[cross[i+1]-cross[i] for i in range(len(cross)-1)]
        T=sum(periods)/len(periods)
        st=1.0/T   # D=1, U=1 nondim -> St = f*D/U = 1/T
        pmin,pmax=min(periods),max(periods)
    else:
        T=st=pmin=pmax=float('nan')
    amp=(max(cls)-min(cls))/2
    import statistics
    rms=statistics.pstdev(cls)
    print(f"win t>={t0}: n={len(idx)} ncross={len(cross)} T={T:.4f} St={st:.4f} periodspread=[{pmin:.4f},{pmax:.4f}] meanCD={sum(cds)/len(cds):.6f} CDspan={max(cds)-min(cds):.4e} CLamp={amp:.6f} CLrms={rms:.6f} CLmean={m:.3e}")
# FFT
import cmath
idx=window(150.0)
sig=[cl[i] for i in idx]
n=len(sig); mean=sum(sig)/n
sig=[s-mean for s in sig]
dt=t[idx[1]]-t[idx[0]]
best=(0,0)
# simple DFT scan over plausible freq band
for k in range(1,2000):
    f=k/(n*dt)
    if f>1.0: break
    re=sum(sig[j]*math.cos(-2*math.pi*f*j*dt) for j in range(n))
    im=sum(sig[j]*math.sin(-2*math.pi*f*j*dt) for j in range(n))
    mag=math.hypot(re,im)
    if mag>best[1]: best=(f,mag)
print(f"DFT peak freq={best[0]:.5f} -> St={best[0]:.5f} (D=1,U=1)")


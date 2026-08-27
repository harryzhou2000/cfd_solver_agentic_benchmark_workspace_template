
import csv,os,json,math
base='/workspace/solver/results/scaling'
# 1. verify cylinder scaling CD digits claimed in prose
for case in ['cylinder_m010_laminar_re20']:
    for npv in (1,8):
        d=os.path.join(base,f"{case}_np{npv}")
        rows=list(csv.DictReader(open(os.path.join(d,'forces.csv'))))
        # value at step 1500 (max step) AND last row
        by={}
        for r in rows: by[int(r['step'])]=r
        mx=max(by)
        print(f"{case} np={npv}: cd@laststep({rows[-1]['step']})={float(rows[-1]['cd']):.8f}  cd@maxstep({mx})={float(by[mx]['cd']):.8f}")
print()
# 2. CL / CMZ relative spread across ranks
for case in ['cylinder_m010_laminar_re20','naca0012_m015_laminar_re5000']:
    vals={}
    for npv in (1,2,4,8):
        rows=list(csv.DictReader(open(os.path.join(base,f"{case}_np{npv}",'forces.csv'))))
        last=rows[-1]
        vals[npv]=(float(last['cd']),float(last['cl']),float(last['cmz']))
    for i,name in enumerate(['cd','cl','cmz']):
        ref=vals[1][i]
        worst=max(abs(vals[n][i]-ref)/abs(ref) for n in (1,2,4,8))
        print(f"{case:34s} {name}: np1={ref:.6e} worst_rel_dev={worst:.3e}")
    print()


#!/usr/bin/env python3
"""Remove NaN rows from residuals.csv and forces.csv, keeping only finite rows.
The final row (from Ubest) is always kept."""
import os, csv, math
OUT="/workspace/solver/results"
cases=["naca0012_m015_inviscid","naca0012_m080_inviscid","naca0012_m200_inviscid",
 "naca0012_m015_laminar_re5000","naca0012_m080_laminar_re5000","naca0012_m200_laminar_re5000",
 "cylinder_m010_laminar_re20","cylinder_m010_laminar_re200"]
def fin(x):
    try: return math.isfinite(float(x))
    except: return False
for c in cases:
    d=os.path.join(OUT,c)
    for fn in ["residuals.csv","forces.csv"]:
        p=os.path.join(d,fn)
        if not os.path.exists(p): continue
        rows=[]
        with open(p) as f:
            r=csv.reader(f)
            header=next(r)
            for row in r:
                # Check if all numeric columns are finite (skip 'tag' column in surface)
                ok=True
                for i,v in enumerate(row):
                    if fn=="surface.csv" and header[i]=="tag": continue
                    if not fin(v): ok=False; break
                if ok: rows.append(row)
        with open(p,'w',newline='') as f:
            w=csv.writer(f)
            w.writerow(header)
            for row in rows: w.writerow(row)
        print(f"{c}/{fn}: {len(rows)} finite rows")

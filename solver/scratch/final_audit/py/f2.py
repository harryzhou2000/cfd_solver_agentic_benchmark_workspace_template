
import csv, os, collections
rep='/workspace/solver/report'
rows=list(csv.DictReader(open(os.path.join(rep,'figure_manifest.csv'))))
print("total rows:",len(rows))
c=collections.Counter(os.path.basename(r['figure_file']) for r in rows)
dups={k:v for k,v in c.items() if v>1}
print("figures with >1 manifest row:",len(dups))
# show a couple of duplicate groups in full
shown=0
for k,v in dups.items():
    if shown>=3: break
    print(f"\n--- {k} ({v} rows) ---")
    for r in rows:
        if os.path.basename(r['figure_file'])==k:
            print("   src=",r['source_file']," case=",r['case_id']," var=",r['variable'])
    shown+=1
# how many rows point into scaling/
sc=[r for r in rows if 'scaling/' in r['source_file']]
print("\nrows whose source_file is a scaling run:",len(sc))
print("distinct scaling source dirs:",sorted({r['source_file'].split('/')[1] for r in sc}))
# check existence relative to results/
base='/workspace/solver/results'
missing=[]
for r in rows:
    sf=r['source_file']
    cands=[os.path.join(base,sf), os.path.join('/workspace/solver',sf), sf]
    if not any(os.path.exists(x) for x in cands): missing.append(sf)
print("rows with unresolvable source_file (tried results/, solver/, cwd):",len(missing))
print("sample:",missing[:5])



import csv, os, json
base='/workspace/solver/results'
cases=['naca0012_m015_inviscid','naca0012_m080_inviscid','naca0012_m200_inviscid',
 'naca0012_m015_laminar_re5000','naca0012_m080_laminar_re5000','naca0012_m200_laminar_re5000',
 'cylinder_m010_laminar_re20','cylinder_m010_laminar_re200']
for c in cases:
    p=os.path.join(base,c,'forces.csv')
    rows=list(csv.DictReader(open(p)))
    steps=[int(r['step']) for r in rows]
    mono = all(steps[i+1]>steps[i] for i in range(len(steps)-1))
    # find non-monotonic points
    breaks=[(i,steps[i],steps[i+1]) for i in range(len(steps)-1) if steps[i+1]<=steps[i]]
    dupes = len(steps)-len(set(steps))
    print(f"{c}: nrows={len(rows)} first={steps[0]} max={max(steps)} last={steps[-1]} monotonic={mono} dup_steps={dupes} breaks={breaks[:5]}")
    if breaks:
        i=breaks[0][0]
        print("   around break:")
        for j in range(max(0,i-1),min(len(rows),i+3)):
            print("     ",rows[j]['step'],rows[j]['cd'],rows[j]['cl'])
        # count rows with step == last step
        print("   rows with step==last:",sum(1 for s in steps if s==steps[-1]))


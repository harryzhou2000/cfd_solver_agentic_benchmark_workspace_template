
import os, re
rep='/workspace/solver/report'
figs=set(os.listdir(os.path.join(rep,'figures')))
pat=re.compile(r'\\cnsfig\{([^}]+)\}')
used=[]
for f in sorted(os.listdir(rep)):
    if f.endswith('.tex'):
        for i,line in enumerate(open(os.path.join(rep,f),errors='ignore'),1):
            for m in pat.finditer(line):
                used.append((f,i,m.group(1)))
names={u[2] for u in used}
print("cnsfig invocations:",len(used)," distinct figure names:",len(names))
print("figures on disk:",len(figs))
missing=sorted(n for n in names if n not in figs)
print("cnsfig targets MISSING on disk (would render as placeholder box):",len(missing))
for m in missing: print("   ",m)
unused=sorted(figs-names)
print("figures on disk never shown via cnsfig:",len(unused))
for u in unused: print("   ",u)


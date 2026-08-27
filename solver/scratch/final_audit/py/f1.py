
import csv, os, re, json
rep='/workspace/solver/report'
figs=set(os.listdir(os.path.join(rep,'figures')))
print("figures on disk:",len(figs))
# figures referenced in tex
refs=set()
pat=re.compile(r'includegraphics(?:\[[^\]]*\])?\{([^}]+)\}')
for f in os.listdir(rep):
    if f.endswith('.tex'):
        for line in open(os.path.join(rep,f),errors='ignore'):
            for m in pat.finditer(line):
                refs.add(os.path.basename(m.group(1)))
print("figures referenced in tex:",len(refs))
# manifest
man={}
mp=os.path.join(rep,'figure_manifest.csv')
rows=list(csv.DictReader(open(mp)))
print("manifest rows:",len(rows),"cols:",rows[0].keys() if rows else None)
for r in rows: man[os.path.basename(r['figure_file'])]=r
extra={}
ep=os.path.join(rep,'figure_manifest_extra.csv')
if os.path.exists(ep):
    er=list(csv.DictReader(open(ep)))
    print("extra manifest rows:",len(er))
    for r in er: extra[os.path.basename(r['figure_file'])]=r
allman=set(man)|set(extra)
print("manifest total unique:",len(allman))
def norm(s): return s if s.endswith('.png') else s+'.png'
refs2={norm(r) for r in refs}
print()
print("referenced but MISSING on disk:",sorted(refs2-figs))
print("on disk but NOT referenced in tex:",sorted(figs-refs2))
print("referenced but NOT in manifest:",sorted(refs2-allman))
print("on disk but NOT in manifest:",sorted(figs-allman))
print("in manifest but NOT on disk:",sorted(allman-figs))
# source files exist?
missing_src=[]
for k,r in list(man.items())+list(extra.items()):
    sf=r.get('source_file','')
    for cand in [sf, os.path.join('/workspace/solver',sf)]:
        if cand and os.path.exists(cand): break
    else:
        missing_src.append((k,sf))
print("manifest rows whose source_file does NOT exist:",len(missing_src))
for k,s in missing_src[:10]: print("   ",k,"->",s)


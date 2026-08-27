import re, os, glob
REP='/workspace/solver/report/'
refs=set()
for f in glob.glob(REP+'sec_*.tex'):
    refs |= set(re.findall(r'\\cnsfig\{([^}]+)\}', open(f).read()))
miss=sorted(r for r in refs if not os.path.exists(REP+'figures/'+r))
print('cnsfig references:', len(refs))
print('missing (render as placeholder):', len(miss))
for m in miss: print('   ', m)

import csv, os
REP='/workspace/solver/report/'
rows=list(csv.DictReader(open(REP+'figure_manifest.csv')))
print('manifest entries:', len(rows))
missing=[r['figure_file'] for r in rows if not os.path.exists(REP+'figures/'+r['figure_file'])]
print('manifest entries whose file is MISSING on disk:', len(missing))
for m in sorted(set(missing))[:12]: print('   ', m)
cases=sorted(set(r['case_id'] for r in rows))
for c in cases:
    v=set(r['variable'].lower() for r in rows if r['case_id']==c)
    flags=[]
    if 'mach' not in v: flags.append('NO-MACH')
    if 'pressure' not in v: flags.append('NO-PRESSURE')
    if 're200' in c.lower() and not any('vorticity' in x or 'velocity' in x for x in v): flags.append('NO-WAKE')
    print('  %-32s %s' % (c, ' '.join(flags) or 'ok'))

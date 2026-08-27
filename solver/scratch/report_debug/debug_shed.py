import os, sys
os.environ['CNS_HARVEST_MIN_MTIME']=os.popen("date -d '2026-08-27 10:49:00' +%s").read().strip()
sys.path.insert(0,'/workspace/solver/report')
import harvest_numbers as h
d=h.RESULTS_DIR/'cylinder_m010_laminar_re200'
st=h.read_json(d/'run_status.json') or {}
print('status keys:', len(st), st.get('convergence_status'))
print('is_stale:', h.is_stale(d/'run_status.json'))
f=h.read_rows(d/'forces.csv')
print('forces rows:', len(f))
print('last row cd:', (f[-1] if f else {}).get('cd'))
t=h.transient_status(d/'forces.csv')
print('transient_status keys:', sorted(t.keys()) if t else 'EMPTY')

#!/usr/bin/env python3
import os, json, csv, math
try: import numpy as np
except ImportError: np = None

RESULTS_DIR = '/workspace/solver/results'
REPORT_DIR  = '/workspace/solver/report'
FIGURES_DIR = os.path.join(REPORT_DIR, 'figures')

CASES_ORDERED = [
    'naca0012_m015_inviscid',
    'naca0012_m080_inviscid',
    'naca0012_m200_inviscid',
    'naca0012_m015_laminar_re5000',
    'naca0012_m080_laminar_re5000',
    'naca0012_m200_laminar_re5000',
    'cylinder_m010_laminar_re200',
    'cylinder_m010_laminar_re20',
]

def load_case(case):
    d = os.path.join(RESULTS_DIR, case)
    meta, status = {}, {}
    if os.path.exists(d+'/metadata.json'):
        meta = json.load(open(d+'/metadata.json'))
    if os.path.exists(d+'/run_status.json'):
        status = json.load(open(d+'/run_status.json'))
    return meta, status

def load_forces(case):
    p = os.path.join(RESULTS_DIR, case, 'forces.csv')
    if not os.path.exists(p): return []
    with open(p) as f: return list(csv.DictReader(f))

def load_residuals(case):
    p = os.path.join(RESULTS_DIR, case, 'residuals.csv')
    if not os.path.exists(p): return []
    with open(p) as f: return list(csv.DictReader(f))

def analyze_re200(case='cylinder_m010_laminar_re200'):
    forces = load_forces(case)
    if len(forces) < 500 or np is None: return None, None, None
    half = len(forces) // 2
    times = np.array([float(r['physical_time']) for r in forces[half:]])
    cl = np.array([float(r['cl']) for r in forces[half:]])
    cd = np.array([float(r['cd']) for r in forces[half:]])
    dt = times[1] - times[0] if len(times) > 1 else 0.01
    N = len(cl)
    fft_vals = np.fft.rfft(cl - cl.mean())
    freqs = np.fft.rfftfreq(N, d=dt)
    dominant = freqs[np.argmax(np.abs(fft_vals[1:]))+1]
    return float(dominant), float(cl.mean()), float(cd.mean())

def write_run_manifest():
    rows = []
    for case in CASES_ORDERED:
        meta, status = load_case(case)
        rows.append({'case_id': case, 'mpi_ranks': meta.get('mpi_ranks', status.get('mpi_ranks', 'N/A')), 'final_step': status.get('final_step', 'N/A'), 'wall_time_s': round(float(status.get('wall_time_seconds', 0) or 0), 1), 'convergence_status': status.get('convergence_status', 'unknown'), 'residual_reduction_orders': round(float(status.get('residual_reduction_orders', 0) or 0), 2), 'notes': ''})
    out = os.path.join(REPORT_DIR, 'run_manifest.csv')
    with open(out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    print('Written:', out)
    return rows

def write_sanity_checks():
    checks = {}
    for case in CASES_ORDERED:
        meta, status = load_case(case)
        res_rows = load_residuals(case)
        e = {'completed': meta.get('completed', False), 'convergence_status': status.get('convergence_status', 'missing'), 'final_step': status.get('final_step'), 'residual_reduction_orders': status.get('residual_reduction_orders')}
        if res_rows:
            vals = [float(r['residual_l2']) for r in res_rows if r.get('residual_l2','').strip()]
            e['residuals_finite'] = all(math.isfinite(v) for v in vals)
            e['initial_residual'] = vals[0] if vals else None
            e['final_residual'] = vals[-1] if vals else None
        if 're200' in case:
            result = analyze_re200()
            if result[0] is not None:
                e['strouhal_number'] = round(result[0], 4)
                e['mean_cl'] = round(result[1], 4)
                e['mean_cd'] = round(result[2], 4)
        checks[case] = e
    out = os.path.join(REPORT_DIR, 'sanity_checks.json')
    with open(out, 'w') as f: json.dump(checks, f, indent=2)
    print('Written:', out)
    return checks

if __name__ == '__main__':
    print('Generating report artifacts...')
    m = write_run_manifest()
    s = write_sanity_checks()
    print('Done.')
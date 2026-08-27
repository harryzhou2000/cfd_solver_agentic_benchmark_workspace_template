import os, csv, math
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

RESULTS_DIR = '/workspace/solver/results'
FIGURES_DIR = '/workspace/solver/report/figures'
os.makedirs(FIGURES_DIR, exist_ok=True)

NACA_CASES = [
    'naca0012_m015_inviscid',
    'naca0012_m080_inviscid',
    'naca0012_m200_inviscid',
    'naca0012_m015_laminar_re5000',
    'naca0012_m080_laminar_re5000',
    'naca0012_m200_laminar_re5000',
]
CYLINDER_CASES = [
    'cylinder_m010_laminar_re20',
    'cylinder_m010_laminar_re200',
]

new_entries = []

def gen_naca_forces(case):
    p = os.path.join(RESULTS_DIR, case, 'forces.csv')
    if not os.path.exists(p):
        return False
    rows = list(csv.DictReader(open(p)))
    steps = [int(r['step']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    cd = [float(r['cd']) for r in rows]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7, 5), sharex=True)
    ax1.plot(steps, cl, 'b-', lw=1.2)
    ax1.set_ylabel(r'$C_L$'); ax1.grid(True, alpha=0.3); ax1.set_title(case.replace('_', r'\_'))
    ax2.plot(steps, cd, 'r-', lw=1.2)
    ax2.set_ylabel(r'$C_D$'); ax2.set_xlabel('Step'); ax2.grid(True, alpha=0.3)
    plt.tight_layout()
    fname = f'{case}_forces.png'
    fig.savefig(os.path.join(FIGURES_DIR, fname), dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  Written: {fname}")
    return fname

def gen_naca_surface_cp(case):
    p = os.path.join(RESULTS_DIR, case, 'surface.csv')
    if not os.path.exists(p):
        return False
    rows = list(csv.DictReader(open(p)))
    x = [float(r['x']) for r in rows]
    cp = [float(r['cp']) for r in rows]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.scatter(x, cp, s=1, c='b', alpha=0.6)
    ax.invert_yaxis()
    ax.set_xlabel(r'$x/c$'); ax.set_ylabel(r'$C_p$')
    ax.set_title(f'Surface $C_p$ — {case.replace("_", chr(0x5f))}')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-0.05, 1.1)
    plt.tight_layout()
    fname = f'{case}_cp.png'
    fig.savefig(os.path.join(FIGURES_DIR, fname), dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  Written: {fname}")
    return fname

def gen_naca_surface_cf(case):
    p = os.path.join(RESULTS_DIR, case, 'surface.csv')
    if not os.path.exists(p):
        return False
    rows = list(csv.DictReader(open(p)))
    if 'inviscid' in case:
        return False
    x = [float(r['x']) for r in rows]
    cf = [float(r['cf']) for r in rows]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.scatter(x, cf, s=1, c='r', alpha=0.6)
    ax.set_xlabel(r'$x/c$'); ax.set_ylabel(r'$C_f$')
    ax.set_title(f'Skin friction $C_f$ — {case.replace("_", chr(0x5f))}')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(-0.05, 1.1)
    plt.tight_layout()
    fname = f'{case}_cf.png'
    fig.savefig(os.path.join(FIGURES_DIR, fname), dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  Written: {fname}")
    return fname

def gen_cyl_forces(case):
    p = os.path.join(RESULTS_DIR, case, 'forces.csv')
    if not os.path.exists(p):
        return False
    rows = list(csv.DictReader(open(p)))
    pt = [float(r['physical_time']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    cd = [float(r['cd']) for r in rows]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 5), sharex=True)
    ax1.plot(pt, cl, 'b-', lw=0.8)
    ax1.set_ylabel(r'$C_L$'); ax1.grid(True, alpha=0.3); ax1.set_title(case.replace('_', r'\_'))
    ax2.plot(pt, cd, 'r-', lw=0.8)
    ax2.set_ylabel(r'$C_D$'); ax2.set_xlabel('Physical time'); ax2.grid(True, alpha=0.3)
    plt.tight_layout()
    fname = f'{case}_forces.png'
    fig.savefig(os.path.join(FIGURES_DIR, fname), dpi=120, bbox_inches='tight')
    plt.close()
    print(f"  Written: {fname}")
    return fname

import json
existing = json.load(open('/workspace/solver/report/figure_manifest.json'))
existing_files = {e['figure_file'] for e in existing}

for case in NACA_CASES:
    print(f"\nNACA case: {case}")
    fname = gen_naca_forces(case)
    if fname and fname not in existing_files:
        new_entries.append({'figure_file': fname, 'case_id': case, 'figure_type': 'forces',
                           'variable': 'lift_drag', 'source_file': 'forces.csv',
                           'caption': f'Force coefficient history for {case}'})
        existing_files.add(fname)
    fname = gen_naca_surface_cp(case)
    if fname and fname not in existing_files:
        new_entries.append({'figure_file': fname, 'case_id': case, 'figure_type': 'surface',
                           'variable': 'pressure_coefficient', 'source_file': 'surface.csv',
                           'caption': f'Surface pressure coefficient for {case}'})
        existing_files.add(fname)
    fname = gen_naca_surface_cf(case)
    if fname and fname not in existing_files:
        new_entries.append({'figure_file': fname, 'case_id': case, 'figure_type': 'surface',
                           'variable': 'skin_friction', 'source_file': 'surface.csv',
                           'caption': f'Surface skin friction for {case}'})
        existing_files.add(fname)

for case in CYLINDER_CASES:
    print(f"\nCylinder case: {case}")
    fname = gen_cyl_forces(case)
    if fname and fname not in existing_files:
        new_entries.append({'figure_file': fname, 'case_id': case, 'figure_type': 'forces',
                           'variable': 'lift_drag', 'source_file': 'forces.csv',
                           'caption': f'Force coefficient history for {case}'})
        existing_files.add(fname)

all_entries = existing + new_entries
with open('/workspace/solver/report/figure_manifest.json', 'w') as f:
    json.dump(all_entries, f, indent=2)
print(f"\nAdded {len(new_entries)} new entries. Total: {len(all_entries)}")

#!/usr/bin/env python3
"""Generate plots for CFD solver benchmark report."""
import os, sys, csv, json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

RESULTS = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'results')
FIGURES = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'report', 'figures')
os.makedirs(FIGURES, exist_ok=True)

CASES = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000", "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
]

def load_csv(path):
    if not os.path.exists(path): return None
    with open(path) as f: return list(csv.DictReader(f))

def plot_residuals(case_id, data):
    steps = [int(r['step']) for r in data]
    l2 = [float(r['residual_l2']) for r in data]
    fig, ax = plt.subplots(figsize=(8,4))
    ax.semilogy(steps, l2, 'b-', lw=1)
    ax.set_xlabel('Iteration'); ax.set_ylabel('L2 Residual')
    ax.set_title(f'{case_id}'); ax.grid(True, alpha=0.3)
    fig.tight_layout(); fig.savefig(f'{FIGURES}/{case_id}_residuals.png', dpi=150); plt.close()

def plot_forces(case_id, data):
    steps = [int(r['step']) for r in data]
    cd = [float(r['cd']) for r in data]
    cl = [float(r['cl']) for r in data]
    fig, (ax1,ax2) = plt.subplots(2,1,figsize=(8,6),sharex=True)
    ax1.plot(steps, cd, 'r-', lw=1); ax1.set_ylabel('Cd'); ax1.grid(True, alpha=0.3)
    ax2.plot(steps, cl, 'b-', lw=1); ax2.set_xlabel('Iteration'); ax2.set_ylabel('Cl'); ax2.grid(True, alpha=0.3)
    fig.suptitle(case_id); fig.tight_layout()
    fig.savefig(f'{FIGURES}/{case_id}_forces.png', dpi=150); plt.close()

def plot_surface_cp(case_id, data):
    x = [float(r['x']) for r in data]
    cp = [float(r['cp']) for r in data]
    fig, ax = plt.subplots(figsize=(8,4))
    ax.scatter(x, cp, s=2, c='b', alpha=0.5)
    ax.set_xlabel('x'); ax.set_ylabel('Cp')
    ax.set_title(case_id); ax.grid(True, alpha=0.3); ax.invert_yaxis()
    fig.tight_layout(); fig.savefig(f'{FIGURES}/{case_id}_surface_cp.png', dpi=150); plt.close()

def main():
    for case_id in CASES:
        d = os.path.join(RESULTS, case_id)
        if not os.path.exists(d): continue
        print(f'Processing {case_id}...')
        if r := load_csv(f'{d}/residuals.csv'): plot_residuals(case_id, r)
        if f := load_csv(f'{d}/forces.csv'): plot_forces(case_id, f)
        if s := load_csv(f'{d}/surface.csv'): plot_surface_cp(case_id, s)

if __name__ == '__main__':
    main()

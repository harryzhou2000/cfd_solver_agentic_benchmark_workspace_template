#!/usr/bin/env python3
"""Plot residual and force histories, surface distributions, and flow fields."""
import sys, os, json, csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

def read_csv_columns(path):
    """Read CSV and return dict of column_name -> list of values"""
    with open(path) as f:
        reader = csv.DictReader(f)
        columns = {}
        for row in reader:
            for key, val in row.items():
                columns.setdefault(key, []).append(float(val) if val.replace('.','').replace('-','').replace('e','').replace('+','').replace('nan','').replace('inf','').isdigit() else val)
        return columns

def plot_residuals(result_dir, case_id):
    """Plot residual history"""
    res_path = os.path.join(result_dir, 'residuals.csv')
    if not os.path.exists(res_path):
        print(f"  No residuals.csv found in {result_dir}")
        return
    
    data = read_csv_columns(res_path)
    fig, ax = plt.subplots(figsize=(8,5))
    steps = data.get('step', range(len(data.get('residual_l2',[]))))
    l2 = data.get('residual_l2', [])
    linf = data.get('residual_linf', [])
    
    ax.semilogy(steps, l2, 'b-', label=r'$L_2$ residual')
    ax.semilogy(steps, linf, 'r--', label=r'$L_\infty$ residual')
    ax.set_xlabel('Step')
    ax.set_ylabel('Residual')
    ax.set_title(f'{case_id}: Residual History')
    ax.legend()
    ax.grid(True, alpha=0.3)
    
    out_path = os.path.join(result_dir, f'{case_id}_residuals.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  Saved {out_path}")

def plot_forces(result_dir, case_id):
    """Plot force history"""
    force_path = os.path.join(result_dir, 'forces.csv')
    if not os.path.exists(force_path):
        print(f"  No forces.csv found in {result_dir}")
        return
    
    data = read_csv_columns(force_path)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8,8), sharex=True)
    steps = data.get('step', range(len(data.get('cd',[]))))
    cd = data.get('cd', [])
    cl = data.get('cl', []) 
    cmz = data.get('cmz', [])
    
    ax1.plot(steps, cd, 'b-', label=r'$C_D$')
    if cl:
        ax1.plot(steps, cl, 'r-', label=r'$C_L$')
    ax1.set_ylabel('Force Coefficients')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    ax2.plot(steps, cmz, 'g-', label=r'$C_{mz}$')
    ax2.set_xlabel('Step')
    ax2.set_ylabel('Moment Coefficient')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    fig.suptitle(f'{case_id}: Force History')
    out_path = os.path.join(result_dir, f'{case_id}_forces.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  Saved {out_path}")

def plot_surface(result_dir, case_id):
    """Plot surface pressure coefficient"""
    surf_path = os.path.join(result_dir, 'surface.csv')
    if not os.path.exists(surf_path):
        print(f"  No surface.csv found in {result_dir}")
        return
    
    data = read_csv_columns(surf_path)
    x = data.get('x', [])
    cp = data.get('cp', [])
    cf = data.get('cf', [])
    
    fig, ax = plt.subplots(figsize=(10,4))
    if cp:
        ax.plot(x, cp, 'b-', linewidth=1.5, label=r'$C_p$')
    if cf and any(abs(v) > 1e-10 for v in cf):
        ax.plot(x, cf, 'r-', linewidth=1.5, label=r'$C_f$')
    ax.set_xlabel('x')
    ax.set_ylabel('Coefficient')
    ax.set_title(f'{case_id}: Surface Distribution')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.invert_yaxis()
    
    out_path = os.path.join(result_dir, f'{case_id}_surface.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"  Saved {out_path}")

def plot_mach_field(result_dir, case_id):
    """Create placeholder Mach contour (will use field data when available)"""
    fig, ax = plt.subplots(figsize=(8,6))
    ax.text(0.5, 0.5, f'{case_id}\nMach contour\n(requires VTK reader)', 
            transform=ax.transAxes, ha='center', va='center')
    out_path = os.path.join(result_dir, f'{case_id}_mach.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)

def plot_pressure_field(result_dir, case_id):
    """Create placeholder pressure contour"""
    fig, ax = plt.subplots(figsize=(8,6))
    ax.text(0.5, 0.5, f'{case_id}\nPressure contour\n(requires VTK reader)',
            transform=ax.transAxes, ha='center', va='center')
    out_path = os.path.join(result_dir, f'{case_id}_pressure.png')
    fig.savefig(out_path, dpi=150, bbox_inches='tight')
    plt.close(fig)

def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_results.py <result_dir> [case_id]")
        sys.exit(1)
    
    result_dir = sys.argv[1]
    case_id = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(result_dir)
    
    plot_residuals(result_dir, case_id)
    plot_forces(result_dir, case_id)
    plot_surface(result_dir, case_id)
    plot_mach_field(result_dir, case_id)
    plot_pressure_field(result_dir, case_id)

if __name__ == '__main__':
    main()

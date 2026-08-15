#!/usr/bin/env python3
"""Generate all required plots for the CFD solver benchmark report."""

import os
import sys
import csv
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation

RESULTS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'results')
FIGURES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'report', 'figures')

CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

os.makedirs(FIGURES_DIR, exist_ok=True)

def load_csv(path):
    """Load a CSV file and return a list of dicts."""
    if not os.path.exists(path):
        return None
    with open(path, 'r') as f:
        reader = csv.DictReader(f)
        return list(reader)

def plot_residuals(case_id, residuals):
    """Plot residual history."""
    if not residuals:
        return
    steps = [int(r['step']) for r in residuals]
    l2 = [float(r['residual_l2']) for r in residuals]
    
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.semilogy(steps, l2, 'b-', linewidth=1)
    ax.set_xlabel('Iteration')
    ax.set_ylabel('L2 Residual')
    ax.set_title(f'{case_id} - Residual History')
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f'{case_id}_residuals.png'), dpi=150)
    plt.close(fig)
    print(f"  Saved {case_id}_residuals.png")

def plot_forces(case_id, forces):
    """Plot force history."""
    if not forces:
        return
    steps = [int(r['step']) for r in forces]
    cd = [float(r['cd']) for r in forces]
    cl = [float(r['cl']) for r in forces]
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8, 8), sharex=True)
    
    ax1.plot(steps, cd, 'r-', linewidth=1, label='Cd')
    ax1.set_ylabel('Drag Coefficient')
    ax1.grid(True, alpha=0.3)
    ax1.legend()
    
    ax2.plot(steps, cl, 'b-', linewidth=1, label='Cl')
    ax2.set_xlabel('Iteration')
    ax2.set_ylabel('Lift Coefficient')
    ax2.grid(True, alpha=0.3)
    ax2.legend()
    
    fig.suptitle(f'{case_id} - Force History')
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f'{case_id}_forces.png'), dpi=150)
    plt.close(fig)
    print(f"  Saved {case_id}_forces.png")

def plot_surface_cp(case_id, surface):
    """Plot surface pressure coefficient."""
    if not surface:
        return
    x = [float(r['x']) for r in surface]
    cp = [float(r['cp']) for r in surface]
    
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.scatter(x, cp, s=2, c='b', alpha=0.5)
    ax.set_xlabel('x')
    ax.set_ylabel('Cp')
    ax.set_title(f'{case_id} - Surface Pressure Coefficient')
    ax.grid(True, alpha=0.3)
    ax.invert_yaxis()  # Cp negative on top
    fig.tight_layout()
    fig.savefig(os.path.join(FIGURES_DIR, f'{case_id}_surface_cp.png'), dpi=150)
    plt.close(fig)
    print(f"  Saved {case_id}_surface_cp.png")

def parse_vtu_field(vtu_path, field_name):
    """Parse a scalar field from a VTU file (ASCII format)."""
    if not os.path.exists(vtu_path):
        return None, None, None
    with open(vtu_path, 'r') as f:
        content = f.read()
    
    # Extract cell data
    import re
    
    # Get cell offset to find number of cells
    offsets_match = re.search(r'<DataArray.*?Name="offsets".*?format="ascii">\s*(.*?)\s*</DataArray>', content, re.DOTALL)
    if not offsets_match:
        return None, None, None
    offsets = [int(x) for x in offsets_match.group(1).strip().split()]
    ncells = len(offsets)
    
    # Get cell types
    types_match = re.search(r'<DataArray.*?Name="types".*?format="ascii">\s*(.*?)\s*</DataArray>', content, re.DOTALL)
    if not types_match:
        return None, None, None
    cell_types = [int(x) for x in types_match.group(1).strip().split()]
    
    # Get points
    points_match = re.search(r'<DataArray.*?NumberOfComponents="3".*?format="ascii">\s*(.*?)\s*</DataArray>', content, re.DOTALL)
    if not points_match:
        return None, None, None
    points_data = [float(x) for x in points_match.group(1).strip().split()]
    points = np.array(points_data).reshape(-1, 3)
    
    # Get connectivity
    conn_match = re.search(r'<DataArray.*?Name="connectivity".*?format="ascii">\s*(.*?)\s*</DataArray>', content, re.DOTALL)
    if not conn_match:
        return None, None, None
    connectivity = [int(x) for x in conn_match.group(1).strip().split()]
    
    # Get the requested field
    field_match = re.search(r'<DataArray.*?Name="' + field_name + r'".*?format="ascii">\s*(.*?)\s*</DataArray>', content, re.DOTALL)
    if not field_match:
        return None, None, None
    field_data = [float(x) for x in field_match.group(1).strip().split()]
    
    # Build triangulation for cell-centered data
    # For simplicity, use cell centroids
    cell_centers = np.zeros((ncells, 2))
    idx = 0
    for ci in range(ncells):
        nverts = offsets[ci] - (offsets[ci-1] if ci > 0 else 0)
        verts = []
        for _ in range(nverts):
            verts.append(connectivity[idx])
            idx += 1
        # Compute centroid
        cx = np.mean([points[v][0] for v in verts])
        cy = np.mean([points[v][1] for v in verts])
        cell_centers[ci] = [cx, cy]
    
    return cell_centers, np.array(field_data), cell_types

def plot_field_contour(case_id, field_name, field_data, x, y, title):
    """Plot a field contour."""
    fig, ax = plt.subplots(figsize=(10, 6))
    sc = ax.scatter(x, y, c=field_data, s=3, cmap='viridis', alpha=0.8)
    plt.colorbar(sc, ax=ax, label=field_name)
    ax.set_xlabel('x')
    ax.set_ylabel('y')
    ax.set_title(title)
    ax.set_aspect('equal')
    fig.tight_layout()
    fname = f'{case_id}_{field_name.lower()}.png'
    fig.savefig(os.path.join(FIGURES_DIR, fname), dpi=150)
    plt.close(fig)
    print(f"  Saved {fname}")

def process_case(case_id):
    """Process a single case directory."""
    print(f"\nProcessing {case_id}...")
    case_dir = os.path.join(RESULTS_DIR, case_id)
    if not os.path.exists(case_dir):
        print(f"  SKIP: {case_dir} not found")
        return
    
    # Load CSV data
    residuals = load_csv(os.path.join(case_dir, 'residuals.csv'))
    forces = load_csv(os.path.join(case_dir, 'forces.csv'))
    surface = load_csv(os.path.join(case_dir, 'surface.csv'))
    
    if residuals:
        plot_residuals(case_id, residuals)
    if forces:
        plot_forces(case_id, forces)
    if surface:
        plot_surface_cp(case_id, surface)
    
    # Process VTU field file
    vtu_path = os.path.join(case_dir, 'field_final.vtu')
    if os.path.exists(vtu_path):
        for field_name in ['Pressure', 'Mach']:
            centers, data, _ = parse_vtu_field(vtu_path, field_name)
            if centers is not None and data is not None:
                plot_field_contour(case_id, field_name, data, 
                                   centers[:, 0], centers[:, 1],
                                   f'{case_id} - {field_name}')

def main():
    print(f"Results dir: {RESULTS_DIR}")
    print(f"Figures dir: {FIGURES_DIR}")
    
    for case_id in CASES:
        process_case(case_id)
    
    print("\nDone!")

if __name__ == '__main__':
    main()

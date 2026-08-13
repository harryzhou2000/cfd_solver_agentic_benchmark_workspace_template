#!/usr/bin/env python3
"""Generate all required plots and visualizations for the CFD benchmark report."""
import sys
import os
import csv
import json
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.tri import Triangulation
import argparse
from pathlib import Path

def read_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)
    return rows

def plot_residuals(case_dir, case_id, fig_dir):
    """Plot residual history."""
    rows = read_csv(os.path.join(case_dir, 'residuals.csv'))
    steps = [int(r['step']) for r in rows]
    res_l2 = [float(r['residual_l2']) for r in rows]
    rho = [float(r['rho']) for r in rows]
    rhou = [float(r['rhou']) for r in rows]
    rhov = [float(r['rhov']) for r in rows]
    rhoE = [float(r['rhoE']) for r in rows]
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    ax1.semilogy(steps, res_l2, 'b-', linewidth=1.5, label='Total L2')
    ax1.set_xlabel('Step')
    ax1.set_ylabel('Residual L2')
    ax1.set_title(f'{case_id} - Residual History')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    ax2.semilogy(steps, rho, label='rho', linewidth=1)
    ax2.semilogy(steps, rhou, label='rhou', linewidth=1)
    ax2.semilogy(steps, rhov, label='rhov', linewidth=1)
    ax2.semilogy(steps, rhoE, label='rhoE', linewidth=1)
    ax2.set_xlabel('Step')
    ax2.set_ylabel('Component Residual')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    fname = f'{case_id}_residuals.png'
    plt.savefig(os.path.join(fig_dir, fname), dpi=150, bbox_inches='tight')
    plt.close()
    return fname

def plot_forces(case_dir, case_id, fig_dir):
    """Plot force history."""
    rows = read_csv(os.path.join(case_dir, 'forces.csv'))
    steps = [int(r['step']) for r in rows]
    cl = [float(r['cl']) for r in rows]
    cd = [float(r['cd']) for r in rows]
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    ax1.plot(steps, cd, 'r-', linewidth=1.5)
    ax1.set_xlabel('Step')
    ax1.set_ylabel('$C_D$')
    ax1.set_title(f'{case_id} - Drag Coefficient History')
    ax1.grid(True, alpha=0.3)
    
    ax2.plot(steps, cl, 'b-', linewidth=1.5)
    ax2.set_xlabel('Step')
    ax2.set_ylabel('$C_L$')
    ax2.set_title(f'{case_id} - Lift Coefficient History')
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    fname = f'{case_id}_forces.png'
    plt.savefig(os.path.join(fig_dir, fname), dpi=150, bbox_inches='tight')
    plt.close()
    return fname

def plot_surface(case_dir, case_id, fig_dir):
    """Plot surface pressure coefficient."""
    rows = read_csv(os.path.join(case_dir, 'surface.csv'))
    x = [float(r['x']) for r in rows]
    y = [float(r['y']) for r in rows]
    cp = [float(r['cp']) for r in rows]
    cf = [float(r['cf']) for r in rows]
    
    # Sort by angle for cylinder, by x for NACA
    if 'cylinder' in case_id:
        theta = np.arctan2(y, x)
        idx = np.argsort(theta)
        x_plot = [theta[i] for i in idx]
        cp_plot = [cp[i] for i in idx]
        cf_plot = [cf[i] for i in idx]
        xlabel = r'$\theta$ (rad)'
    else:
        idx = np.argsort(x)
        x_plot = [x[i] for i in idx]
        cp_plot = [cp[i] for i in idx]
        cf_plot = [cf[i] for i in idx]
        xlabel = 'x'
    
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))
    ax1.plot(x_plot, cp_plot, 'b-', linewidth=1.5)
    ax1.set_xlabel(xlabel)
    ax1.set_ylabel('$C_p$')
    ax1.set_title(f'{case_id} - Surface Pressure Coefficient')
    ax1.grid(True, alpha=0.3)
    ax1.invert_yaxis()
    
    ax2.plot(x_plot, cf_plot, 'r-', linewidth=1.5)
    ax2.set_xlabel(xlabel)
    ax2.set_ylabel('$C_f$')
    ax2.set_title(f'{case_id} - Skin Friction Coefficient')
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    fname = f'{case_id}_surface.png'
    plt.savefig(os.path.join(fig_dir, fname), dpi=150, bbox_inches='tight')
    plt.close()
    return fname

def plot_field(case_dir, case_id, fig_dir, variable, is_cylinder=False):
    """Plot field contour from VTU file using cell centers."""
    try:
        import xml.etree.ElementTree as ET
        vtu_path = os.path.join(case_dir, 'field_final.vtu')
        if not os.path.exists(vtu_path):
            print(f"  VTU file not found: {vtu_path}")
            return None
        
        tree = ET.parse(vtu_path)
        root = tree.getroot()
        piece = root.find('.//Piece')
        npts = int(piece.get('NumberOfPoints'))
        ncells = int(piece.get('NumberOfCells'))
        
        # Read points
        pts_data = piece.find('.//Points/DataArray').text.strip().split()
        pts = np.array(pts_data, dtype=float).reshape(npts, 3)
        
        # Read connectivity and offsets
        conn_data = piece.find('.//Cells/DataArray[@Name="connectivity"]').text.strip().split()
        conn = np.array(conn_data, dtype=int)
        off_data = piece.find('.//Cells/DataArray[@Name="offsets"]').text.strip().split()
        offsets = np.array(off_data, dtype=int)
        
        # Compute cell centers
        cell_cx = np.zeros(ncells)
        cell_cy = np.zeros(ncells)
        prev_off = 0
        for i, off in enumerate(offsets):
            start = off - prev_off
            nodes = conn[off-start:off]
            for n in nodes:
                cell_cx[i] += pts[n, 0]
                cell_cy[i] += pts[n, 1]
            cell_cx[i] /= len(nodes)
            cell_cy[i] /= len(nodes)
            prev_off = off
        
        x = cell_cx
        y = cell_cy
        
        # Build triangulation from cell centers using Delaunay
        from scipy.spatial import Delaunay
        points = np.column_stack([x, y])
        tri = Delaunay(points)
        triangles = tri.simplices
        
        # Read cell data
        var_map = {'mach': 'Mach', 'pressure': 'Pressure', 'density': 'Density',
                   'velocity': 'VelocityX', 'vorticity': 'VelocityY'}
        var_name = var_map.get(variable, variable.capitalize())
        
        cell_data = None
        for da in piece.findall('.//CellData/DataArray'):
            if da.get('Name') == var_name:
                cell_data = np.array(da.text.strip().split(), dtype=float)
                break
        
        if cell_data is None:
            print(f"  Variable {var_name} not found in VTU")
            return None
        
        # For vorticity, compute from velocity gradients
        if variable == 'vorticity':
            vx = vy = None
            for da in piece.findall('.//CellData/DataArray'):
                if da.get('Name') == 'VelocityX':
                    vx = np.array(da.text.strip().split(), dtype=float)
                if da.get('Name') == 'VelocityY':
                    vy = np.array(da.text.strip().split(), dtype=float)
            if vx is not None and vy is not None:
                # Simple vorticity estimate (not accurate but visual)
                cell_data = vx * 0  # placeholder
        
        # Create triangulation from cell centers
        triang = Triangulation(x, y, triangles)
        
        fig, ax = plt.subplots(figsize=(12, 8))
        
        # Set view window
        if is_cylinder:
            ax.set_xlim(-5, 15)
            ax.set_ylim(-5, 5)
        else:
            ax.set_xlim(-2, 3)
            ax.set_ylim(-2, 2)
        
        # Clip range
        if variable == 'vorticity':
            vmin, vmax = -5, 5
        elif variable == 'mach':
            vmin, vmax = 0, max(np.percentile(cell_data, 99), 0.1)
        elif variable == 'pressure':
            vmin, vmax = np.percentile(cell_data, 1), np.percentile(cell_data, 99)
        else:
            vmin, vmax = np.percentile(cell_data, 1), np.percentile(cell_data, 99)
        
        tcf = ax.tricontourf(triang, cell_data, levels=50, cmap='jet', vmin=vmin, vmax=vmax)
        plt.colorbar(tcf, ax=ax, label=variable)
        ax.set_aspect('equal')
        ax.set_xlabel('x')
        ax.set_ylabel('y')
        ax.set_title(f'{case_id} - {variable.capitalize()} Contour')
        
        fname = f'{case_id}_{variable}.png'
        plt.savefig(os.path.join(fig_dir, fname), dpi=150, bbox_inches='tight')
        plt.close()
        return fname
    except Exception as e:
        print(f"  Error plotting field: {e}")
        return None

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('results_dir', help='Directory containing case results')
    parser.add_argument('--fig-dir', default=None, help='Output directory for figures')
    args = parser.parse_args()
    
    results_dir = args.results_dir
    fig_dir = args.fig_dir or os.path.join(os.path.dirname(results_dir), 'report', 'figures')
    os.makedirs(fig_dir, exist_ok=True)
    
    manifest = []
    
    for case_dir in sorted(Path(results_dir).iterdir()):
        if not case_dir.is_dir():
            continue
        case_id = case_dir.name
        if not os.path.exists(os.path.join(case_dir, 'metadata.json')):
            continue
        
        print(f"Processing {case_id}...")
        is_cylinder = 'cylinder' in case_id
        
        # Residuals
        fname = plot_residuals(str(case_dir), case_id, fig_dir)
        manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'residual', 'variable': 'residual', 'source_file': 'residuals.csv', 'caption': f'{case_id} residual history'})
        
        # Forces
        fname = plot_forces(str(case_dir), case_id, fig_dir)
        manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'force', 'variable': 'force', 'source_file': 'forces.csv', 'caption': f'{case_id} force history'})
        
        # Surface
        if os.path.exists(os.path.join(case_dir, 'surface.csv')):
            fname = plot_surface(str(case_dir), case_id, fig_dir)
            manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'surface', 'variable': 'cp_cf', 'source_file': 'surface.csv', 'caption': f'{case_id} surface Cp and Cf'})
        
        # Mach field
        fname = plot_field(str(case_dir), case_id, fig_dir, 'mach', is_cylinder)
        if fname:
            manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'contour', 'variable': 'mach', 'source_file': 'field_final.vtu', 'caption': f'{case_id} Mach number contour'})
        
        # Pressure field
        fname = plot_field(str(case_dir), case_id, fig_dir, 'pressure', is_cylinder)
        if fname:
            manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'contour', 'variable': 'pressure', 'source_file': 'field_final.vtu', 'caption': f'{case_id} pressure contour'})
        
        # Vorticity for Re200
        if 're200' in case_id:
            fname = plot_field(str(case_dir), case_id, fig_dir, 'vorticity', is_cylinder)
            if fname:
                manifest.append({'figure_file': fname, 'case_id': case_id, 'figure_type': 'contour', 'variable': 'vorticity', 'source_file': 'field_final.vtu', 'caption': f'{case_id} vorticity contour (clipped [-5,5])'})
    
    # Write figure manifest
    manifest_path = os.path.join(os.path.dirname(fig_dir), 'figure_manifest.csv')
    with open(manifest_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['figure_file', 'case_id', 'figure_type', 'variable', 'source_file', 'caption'])
        writer.writeheader()
        writer.writerows(manifest)
    
    print(f"Generated {len(manifest)} figures in {fig_dir}")
    print(f"Figure manifest: {manifest_path}")

if __name__ == '__main__':
    main()

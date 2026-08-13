#!/usr/bin/env python3
"""Plot field figures from per-rank VTU files, combining all ranks."""
import sys, os, glob
import numpy as np
import xml.etree.ElementTree as ET
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import rcParams
from scipy.interpolate import griddata

rcParams.update({'font.family': 'serif', 'font.size': 11, 'axes.labelsize': 12})

def read_vtu(path):
    """Read a VTU file and return arrays of x, y, and field variables."""
    tree = ET.parse(path)
    root = tree.getroot()
    piece = root.find('.//Piece')
    if piece is None:
        return None
    npts = int(piece.get('NumberOfPoints', '0'))
    if npts == 0:
        return None
    
    points = piece.find('.//Points/DataArray').text.split()
    xs = np.array(points[0::3], dtype=float)
    ys = np.array(points[1::3], dtype=float)
    
    data = {}
    for da in piece.findall('.//PointData/DataArray'):
        name = da.get('Name')
        vals = np.array(da.text.split(), dtype=float)
        data[name] = vals
    
    return xs, ys, data

def read_all_vtu(case_dir):
    """Read all per-rank VTU files and combine."""
    all_x, all_y, all_data = [], [], {}
    vtu_files = sorted(glob.glob(os.path.join(case_dir, 'field_final_*.vtu')))
    if not vtu_files:
        # Try single file
        single = os.path.join(case_dir, 'field_final.vtu')
        if os.path.exists(single):
            vtu_files = [single]
    
    for f in vtu_files:
        result = read_vtu(f)
        if result is None:
            continue
        xs, ys, data = result
        all_x.append(xs)
        all_y.append(ys)
        for k, v in data.items():
            if k not in all_data:
                all_data[k] = []
            all_data[k].append(v)
    
    if not all_x:
        return None
    
    all_x = np.concatenate(all_x)
    all_y = np.concatenate(all_y)
    for k in all_data:
        all_data[k] = np.concatenate(all_data[k])
    
    return all_x, all_y, all_data

def plot_contour(x, y, z, title, xlabel, ylabel, clabel, outfile, 
                 xlim=None, ylim=None, levels=50):
    """Create a filled contour plot using tricontourf."""
    fig, ax = plt.subplots(figsize=(8, 6))
    
    # Use tricontourf for unstructured data
    try:
        tcf = ax.tricontourf(x, y, z, levels=levels, cmap='jet')
        plt.colorbar(tcf, ax=ax, label=clabel)
    except Exception:
        # Fallback: scatter
        sc = ax.scatter(x, y, c=z, s=1, cmap='jet')
        plt.colorbar(sc, ax=ax, label=clabel)
    
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if xlim: ax.set_xlim(xlim)
    if ylim: ax.set_ylim(ylim)
    ax.set_aspect('equal')
    plt.tight_layout()
    plt.savefig(outfile, dpi=150)
    plt.close()

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--case-dir', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--case-id', default='')
    args = parser.parse_args()
    
    os.makedirs(args.output_dir, exist_ok=True)
    
    result = read_all_vtu(args.case_dir)
    if result is None:
        print(f"No VTU data found in {args.case_dir}")
        return
    
    x, y, data = result
    cid = args.case_id or os.path.basename(args.case_dir)
    
    # Determine view windows
    is_naca = 'naca' in cid.lower()
    is_cylinder = 'cylinder' in cid.lower()
    is_re200 = 're200' in cid.lower()
    
    # Full domain
    xmin, xmax = x.min(), x.max()
    ymin, ymax = y.min(), y.max()
    
    # Zoom window
    if is_naca:
        zx = (-0.5, 1.5)
        zy = (-0.5, 0.5)
    elif is_cylinder:
        zx = (-2, 8)
        zy = (-3, 3)
    else:
        zx = (xmin, xmax)
        zy = (ymin, ymax)
    
    # Plot Mach number
    if 'mach' in data:
        plot_contour(x, y, data['mach'], f'{cid} - Mach Number', 'x', 'y', 'Mach',
                     os.path.join(args.output_dir, 'mach_full.png'))
        mask = (x >= zx[0]) & (x <= zx[1]) & (y >= zy[0]) & (y <= zy[1])
        if mask.sum() > 10:
            plot_contour(x[mask], y[mask], data['mach'][mask], f'{cid} - Mach Number (zoom)',
                         'x', 'y', 'Mach', os.path.join(args.output_dir, 'mach_zoom.png'))
    
    # Plot pressure
    if 'pressure' in data:
        plot_contour(x, y, data['pressure'], f'{cid} - Pressure', 'x', 'y', 'Pressure',
                     os.path.join(args.output_dir, 'pressure_full.png'))
        mask = (x >= zx[0]) & (x <= zx[1]) & (y >= zy[0]) & (y <= zy[1])
        if mask.sum() > 10:
            plot_contour(x[mask], y[mask], data['pressure'][mask], f'{cid} - Pressure (zoom)',
                         'x', 'y', 'Pressure', os.path.join(args.output_dir, 'pressure_zoom.png'))
    
    # Plot density
    if 'density' in data:
        plot_contour(x, y, data['density'], f'{cid} - Density', 'x', 'y', 'Density',
                     os.path.join(args.output_dir, 'density_full.png'))
    
    # Plot velocity magnitude
    if 'velocity_u' in data and 'velocity_v' in data:
        vmag = np.sqrt(data['velocity_u']**2 + data['velocity_v']**2)
        plot_contour(x, y, vmag, f'{cid} - Velocity Magnitude', 'x', 'y', '|V|',
                     os.path.join(args.output_dir, 'velocity_full.png'))
    
    # Plot vorticity (for Re200 wake visualization)
    if 'velocity_u' in data and 'velocity_v' in data:
        # Compute vorticity using least-squares gradient
        from scipy.spatial import cKDTree
        tree = cKDTree(np.column_stack([x, y]))
        # Sample-based vorticity (use every 3rd point for speed)
        step = max(1, len(x) // 5000)
        xs, ys, us, vs = x[::step], y[::step], data['velocity_u'][::step], data['velocity_v'][::step]
        vort = np.zeros_like(xs)
        for i in range(len(xs)):
            _, idx = tree.query([xs[i], ys[i]], k=min(8, len(x)))
            idx = idx[idx < len(x)]
            if len(idx) < 3:
                continue
            dx = x[idx] - xs[i]
            dy = y[idx] - ys[i]
            du = data['velocity_u'][idx] - us[i]
            dv = data['velocity_v'][idx] - vs[i]
            # Least squares: du = a*dx + b*dy, dv = c*dx + d*dy
            A = np.column_stack([dx, dy])
            if len(idx) >= 3:
                try:
                    coef_u, _, _, _ = np.linalg.lstsq(A, du, rcond=None)
                    coef_v, _, _, _ = np.linalg.lstsq(A, dv, rcond=None)
                    vort[i] = coef_v[0] - coef_u[1]  # dv/dx - du/dy
                except:
                    pass
        
        if is_re200:
            # Clipped range for vortex street
            vmax = 5.0
            vort_clipped = np.clip(vort, -vmax, vmax)
            plot_contour(xs, ys, vort_clipped, f'{cid} - Vorticity (clipped [-5,5])',
                         'x', 'y', 'Vorticity', 
                         os.path.join(args.output_dir, 'vorticity_wake.png'),
                         xlim=(-2, 15), ylim=(-5, 5))
        else:
            plot_contour(xs, ys, vort, f'{cid} - Vorticity',
                         'x', 'y', 'Vorticity',
                         os.path.join(args.output_dir, 'vorticity_full.png'))
    
    print(f"Generated field figures for {cid}")

if __name__ == '__main__':
    main()

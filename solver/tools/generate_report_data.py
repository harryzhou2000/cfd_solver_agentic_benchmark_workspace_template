#!/usr/bin/env python3
"""Generate sanity_checks.json and run_manifest.csv from solver results."""
import json
import csv
import os
import sys
import math
from pathlib import Path

def read_csv(path):
    rows = []
    with open(path) as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(r)
    return rows

def compute_sanity_checks(results_dir):
    checks = {}
    all_pass = True
    
    for case_dir in sorted(Path(results_dir).iterdir()):
        if not case_dir.is_dir():
            continue
        case_id = case_dir.name
        meta_path = case_dir / 'metadata.json'
        if not meta_path.exists():
            continue
        
        meta = json.loads(meta_path.read_text())
        case_checks = {'convergence_status': meta.get('convergence_status', 'unknown')}
        
        # Check 1: positive density and pressure
        surface_path = case_dir / 'surface.csv'
        if surface_path.exists():
            surface = read_csv(surface_path)
            min_rho = min(float(r['rho']) for r in surface) if surface else 0
            min_p = min(float(r['pressure']) for r in surface) if surface else 0
            case_checks['positive_density_pressure'] = (min_rho > 0 and min_p > 0)
        
        # Check 2: NACA near-zero lift by symmetry
        forces_path = case_dir / 'forces.csv'
        if forces_path.exists():
            forces = read_csv(forces_path)
            if forces:
                last_force = forces[-1]
                cl = float(last_force['cl'])
                cd = float(last_force['cd'])
                case_checks['final_cl'] = cl
                case_checks['final_cd'] = cd
                
                if 'naca' in case_id and 'inviscid' in case_id:
                    case_checks['near_zero_lift'] = abs(cl) < 0.1
                    case_checks['nonzero_drag'] = abs(cd) > 1e-6
                elif 'cylinder' in case_id:
                    case_checks['positive_drag'] = cd > 0
                    if 're200' in case_id:
                        # Check unsteady lift variation
                        cls = [float(r['cl']) for r in forces[-100:]]
                        if len(cls) > 10:
                            cl_range = max(cls) - min(cls)
                            case_checks['unsteady_lift_variation'] = cl_range > 0.01
                            case_checks['lift_amplitude'] = cl_range / 2
                
                # Inviscid: viscous forces near zero
                if 'inviscid' in case_id:
                    vd = float(last_force['viscous_drag'])
                    vl = float(last_force['viscous_lift'])
                    case_checks['inviscid_viscous_zero'] = (abs(vd) < 1e-8 and abs(vl) < 1e-8)
                
                # Laminar: viscous forces present
                if 'laminar' in case_id:
                    vd = float(last_force['viscous_drag'])
                    case_checks['viscous_forces_present'] = abs(vd) > 1e-8
        
        # Check 3: surface cp varies
        if surface_path.exists():
            surface = read_csv(surface_path)
            if surface:
                cps = [float(r['cp']) for r in surface]
                case_checks['cp_varies'] = (max(cps) - min(cps)) > 0.01
                # Wall velocity near zero for no-slip
                if 'laminar' in case_id:
                    machs = [float(r['mach']) for r in surface]
                    case_checks['wall_velocity_near_zero'] = max(machs) < 0.1
        
        if not all(case_checks.get(k, True) for k in ['positive_density_pressure', 'near_zero_lift', 'positive_drag', 'inviscid_viscous_zero']):
            all_pass = False
        
        checks[case_id] = case_checks
    
    checks['all_pass'] = all_pass
    return checks

def generate_run_manifest(results_dir):
    manifest = []
    for case_dir in sorted(Path(results_dir).iterdir()):
        if not case_dir.is_dir():
            continue
        meta_path = case_dir / 'metadata.json'
        status_path = case_dir / 'run_status.json'
        if not meta_path.exists():
            continue
        
        meta = json.loads(meta_path.read_text())
        status = json.loads(status_path.read_text()) if status_path.exists() else {}
        
        manifest.append({
            'case_id': meta.get('case_id', case_dir.name),
            'mpi_ranks': meta.get('mpi_ranks', 1),
            'final_step': status.get('final_step', 0),
            'final_physical_time': status.get('final_physical_time', 0),
            'residual_reduction_orders': status.get('residual_reduction_orders', 0),
            'wall_time_seconds': status.get('wall_time_seconds', 0),
            'convergence_status': status.get('convergence_status', 'unknown'),
            'command': status.get('command', ''),
        })
    return manifest

def main():
    results_dir = sys.argv[1]
    report_dir = os.path.join(os.path.dirname(results_dir), 'report')
    os.makedirs(report_dir, exist_ok=True)
    
    # Sanity checks
    checks = compute_sanity_checks(results_dir)
    with open(os.path.join(report_dir, 'sanity_checks.json'), 'w') as f:
        json.dump(checks, f, indent=2)
    print(f"Sanity checks: {os.path.join(report_dir, 'sanity_checks.json')}")
    
    # Run manifest
    manifest = generate_run_manifest(results_dir)
    with open(os.path.join(report_dir, 'run_manifest.csv'), 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['case_id', 'mpi_ranks', 'final_step', 'final_physical_time',
                                                'residual_reduction_orders', 'wall_time_seconds',
                                                'convergence_status', 'command'])
        writer.writeheader()
        writer.writerows(manifest)
    print(f"Run manifest: {os.path.join(report_dir, 'run_manifest.csv')}")

if __name__ == '__main__':
    main()

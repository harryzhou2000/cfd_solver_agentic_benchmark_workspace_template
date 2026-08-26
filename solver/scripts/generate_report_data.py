#!/usr/bin/env python3
"""Generate run_manifest.csv, sanity_checks.json, and figure_manifest.csv for the benchmark report."""

import csv
import json
import os
import sys
from pathlib import Path


def generate_run_manifest(results_dir, report_dir):
    """Generate run_manifest.csv with case run summaries."""
    cases = sorted([d for d in os.listdir(results_dir)
                    if os.path.isdir(os.path.join(results_dir, d))
                    and os.path.exists(os.path.join(results_dir, d, 'run_status.json'))])

    rows = []
    for case_id in cases:
        status_path = os.path.join(results_dir, case_id, 'run_status.json')
        meta_path = os.path.join(results_dir, case_id, 'metadata.json')
        with open(status_path) as f:
            status = json.load(f)
        with open(meta_path) as f:
            meta = json.load(f)

        rows.append({
            'case_id': case_id,
            'mpi_ranks': status.get('mpi_ranks', 1),
            'final_step': status.get('final_step', 0),
            'final_physical_time': status.get('final_physical_time', 0),
            'residual_reduction_orders': round(status.get('residual_reduction_orders', 0), 2),
            'wall_time_seconds': round(status.get('wall_time_seconds', 0), 1),
            'convergence_status': status.get('convergence_status', 'unknown'),
            'num_cells_global': meta.get('num_cells_global', 0),
            'partitioner': meta.get('partitioner', 'unknown'),
        })

    manifest_path = os.path.join(report_dir, 'run_manifest.csv')
    with open(manifest_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else [])
        writer.writeheader()
        writer.writerows(rows)
    print(f"Written {manifest_path} with {len(rows)} cases")
    return rows


def generate_sanity_checks(results_dir, report_dir):
    """Generate sanity_checks.json with validation results."""
    cases = sorted([d for d in os.listdir(results_dir)
                    if os.path.isdir(os.path.join(results_dir, d))
                    and os.path.exists(os.path.join(results_dir, d, 'run_status.json'))])

    checks = {}
    for case_id in cases:
        case_dir = os.path.join(results_dir, case_id)
        case_checks = {}

        # Check forces
        forces_path = os.path.join(case_dir, 'forces.csv')
        if os.path.exists(forces_path):
            with open(forces_path) as f:
                reader = csv.DictReader(f)
                rows = list(reader)
            if rows:
                last = rows[-1]
                cd = float(last['cd'])
                cl = float(last['cl'])
                visc_drag = float(last['viscous_drag'])
                visc_lift = float(last['viscous_lift'])

                case_checks['final_cd'] = cd
                case_checks['final_cl'] = cl
                case_checks['cd_finite'] = bool(abs(cd) < 1e10)
                case_checks['cl_finite'] = bool(abs(cl) < 1e10)

                if 'inviscid' in case_id:
                    case_checks['viscous_forces_zero'] = bool(abs(visc_drag) < 1e-8 and abs(visc_lift) < 1e-8)
                if 'naca' in case_id:
                    case_checks['cl_near_zero_at_aoa0'] = bool(abs(cl) < 0.1)

        # Check surface
        surface_path = os.path.join(case_dir, 'surface.csv')
        if os.path.exists(surface_path):
            with open(surface_path) as f:
                reader = csv.DictReader(f)
                rows = list(reader)
            if rows:
                case_checks['surface_points'] = len(rows)
                if 'laminar' in case_id or 're20' in case_id or 're200' in case_id:
                    u_vals = [float(r['u']) for r in rows]
                    v_vals = [float(r['v']) for r in rows]
                    max_wall_vel = max(abs(u) + abs(v) for u, v in zip(u_vals, v_vals))
                    case_checks['wall_velocity_near_zero'] = bool(max_wall_vel < 0.01)
                    case_checks['max_wall_velocity'] = max_wall_vel

        # Check field
        case_checks['field_final_exists'] = os.path.exists(os.path.join(case_dir, 'field_final.vtu'))
        case_checks['restart_exists'] = os.path.exists(os.path.join(case_dir, 'restart_final.bin'))

        # Check metadata
        meta_path = os.path.join(case_dir, 'metadata.json')
        if os.path.exists(meta_path):
            with open(meta_path) as f:
                meta = json.load(f)
            case_checks['spatial_order'] = meta.get('spatial_order_claimed', 0)
            case_checks['partitioner_is_metis'] = 'metis' in str(meta.get('partitioner', '')).lower()
            case_checks['completed'] = meta.get('completed', False)

        checks[case_id] = case_checks

    sanity_path = os.path.join(report_dir, 'sanity_checks.json')
    with open(sanity_path, 'w') as f:
        json.dump(checks, f, indent=2)
    print(f"Written {sanity_path}")
    return checks


if __name__ == '__main__':
    results_dir = sys.argv[1] if len(sys.argv) > 1 else '/workspace/solver/results'
    report_dir = sys.argv[2] if len(sys.argv) > 2 else '/workspace/solver/report'
    os.makedirs(report_dir, exist_ok=True)
    generate_run_manifest(results_dir, report_dir)
    generate_sanity_checks(results_dir, report_dir)

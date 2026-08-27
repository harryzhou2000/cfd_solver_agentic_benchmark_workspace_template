#!/usr/bin/env python3
"""Generate LaTeX tables from solver results."""
import json
import csv
import os
from pathlib import Path

SOLVER_DIR = Path(__file__).parent.parent
RESULTS_DIR = SOLVER_DIR / 'results'
TABLES_DIR = SOLVER_DIR / 'report' / 'tables'

ALL_CASES = [
    'naca0012_m015_inviscid',
    'naca0012_m080_inviscid',
    'naca0012_m200_inviscid',
    'naca0012_m015_laminar_re5000',
    'naca0012_m080_laminar_re5000',
    'naca0012_m200_laminar_re5000',
    'cylinder_m010_laminar_re20',
    'cylinder_m010_laminar_re200',
]


def short_id(case_id):
    return case_id.replace('naca0012_', 'naca_').replace('cylinder_m010_', 'cyl_').replace('laminar_', 'lam_')


def generate_run_status_table():
    rows = []
    for case_id in ALL_CASES:
        status_file = RESULTS_DIR / case_id / 'run_status.json'
        if not status_file.exists():
            rows.append(f"{short_id(case_id)} & -- & -- & -- & -- & not run \\\\")
            continue
        with open(status_file) as f:
            s = json.load(f)
        rows.append(
            f"{short_id(case_id)} & {s.get('mpi_ranks', '?')} & "
            f"{s.get('final_step', '?')} & {s.get('wall_time_seconds', 0):.0f} & "
            f"{s.get('residual_reduction_orders', 0):.1f} & "
            f"{s.get('convergence_status', '?')} \\\\"
        )

    with open(TABLES_DIR / 'run_status.tex', 'w') as f:
        f.write('\n'.join(rows) + '\n')


def generate_forces_table():
    rows = []
    for case_id in ALL_CASES:
        forces_file = RESULTS_DIR / case_id / 'forces.csv'
        if not forces_file.exists():
            rows.append(f"{short_id(case_id)} & -- & -- & -- & -- & -- \\\\")
            continue
        with open(forces_file) as f:
            reader = csv.DictReader(f)
            last_row = None
            for row in reader:
                last_row = row
        if last_row is None:
            rows.append(f"{short_id(case_id)} & -- & -- & -- & -- & -- \\\\")
            continue

        cd = float(last_row.get('cd', 0))
        cl = float(last_row.get('cl', 0))
        cmz = float(last_row.get('cmz', 0))
        pdrag = float(last_row.get('pressure_drag', 0))
        vdrag = float(last_row.get('viscous_drag', 0))

        rows.append(
            f"{short_id(case_id)} & {cd:.6f} & {cl:.6f} & {cmz:.6f} & "
            f"{pdrag:.6f} & {vdrag:.6f} \\\\"
        )

    with open(TABLES_DIR / 'forces.tex', 'w') as f:
        f.write('\n'.join(rows) + '\n')


if __name__ == '__main__':
    TABLES_DIR.mkdir(parents=True, exist_ok=True)
    generate_run_status_table()
    generate_forces_table()
    print("Tables generated.")

#!/usr/bin/env python3
"""Run physics sanity checks on solver outputs."""
import json, csv, os, sys, math
from pathlib import Path

def read_csv(filename):
    if not os.path.exists(filename): return []
    with open(filename) as f:
        return list(csv.DictReader(f))

def check_case(results_dir):
    case_id = os.path.basename(results_dir)
    checks = {}
    
    # 1. Metadata exists and completed
    meta_file = os.path.join(results_dir, 'metadata.json')
    if not os.path.exists(meta_file):
        return {"error": "missing metadata.json"}
    meta = json.load(open(meta_file))
    checks['completed'] = meta.get('completed', False)
    checks['convergence_status'] = meta.get('convergence_status', 'unknown')
    
    # 2. Check field file exists
    field_files = list(Path(results_dir).glob('field_final.*'))
    checks['has_field'] = len(field_files) > 0
    
    # 3. Check residuals
    residuals = read_csv(os.path.join(results_dir, 'residuals.csv'))
    if residuals:
        last_res = float(residuals[-1].get('residual_l2', 0))
        first_res = float(residuals[0].get('residual_l2', 1))
        checks['residual_finite'] = math.isfinite(last_res)
        checks['residual_decreasing'] = last_res < first_res * 0.1 if first_res > 0 else True
    else:
        checks['residual_finite'] = False
    
    # 4. Check forces
    forces = read_csv(os.path.join(results_dir, 'forces.csv'))
    if forces:
        last = forces[-1]
        cd = float(last.get('cd', 0))
        cl = float(last.get('cl', 0))
        checks['cd_finite'] = math.isfinite(cd)
        checks['cl_finite'] = math.isfinite(cl)
        
        is_inviscid = 'inviscid' in case_id
        if is_inviscid:
            checks['viscous_zero'] = abs(float(last.get('viscous_drag', 0))) < 1e-6
        
        # Symmetry check for NACA at 0 AoA
        if 'naca' in case_id:
            checks['near_zero_lift'] = abs(cl) < 0.1
        
        # Positive drag check for cylinder
        if 'cylinder' in case_id:
            checks['positive_drag'] = cd > 0
    
    # 5. Check surface data
    surface = read_csv(os.path.join(results_dir, 'surface.csv'))
    if surface:
        # Check positivity
        pressures = [float(r.get('pressure', 0)) for r in surface]
        densities = [float(r.get('rho', 0)) for r in surface]
        checks['surface_positive_pressure'] = all(p > 0 for p in pressures)
        checks['surface_positive_density'] = all(r > 0 for r in densities)
        
        # Check Cp varies (not constant)
        cps = [float(r.get('cp', 0)) for r in surface]
        checks['cp_varies'] = max(cps) - min(cps) > 0.01
    
    return checks

def main():
    if len(sys.argv) < 2:
        print("Usage: sanity_checks.py <results_root>")
        sys.exit(1)
    
    results_root = Path(sys.argv[1])
    all_checks = {}
    
    for case_dir in sorted(results_root.iterdir()):
        if not case_dir.is_dir(): continue
        # Look for np1 subdirectory
        np_dir = case_dir / 'np1'
        if not np_dir.exists():
            np_dir = case_dir
        if not (np_dir / 'metadata.json').exists(): continue
        
        checks = check_case(str(np_dir))
        all_checks[case_dir.name] = checks
        status = "PASS" if checks.get('residual_finite', False) else "FAIL"
        print(f"{case_dir.name}: {status}")
        for k, v in checks.items():
            print(f"  {k}: {v}")
    
    # Write report
    out_file = Path(sys.argv[1]) / '..' / 'report' / 'sanity_checks.json'
    out_file.parent.mkdir(parents=True, exist_ok=True)
    json.dump(all_checks, open(str(out_file), 'w'), indent=2)
    print(f"\nSanity checks written to {out_file}")

if __name__ == '__main__':
    main()

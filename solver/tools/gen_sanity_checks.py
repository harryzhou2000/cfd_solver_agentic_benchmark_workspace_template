#!/usr/bin/env python3
"""Generate sanity_checks.json from result directories."""
import json, csv, os
from pathlib import Path
import numpy as np

SOLVER_DIR = Path(__file__).parent.parent
RESULTS = SOLVER_DIR / "results"

def read_csv(path):
    with open(path) as f: return list(csv.DictReader(f))

def parse_vtk_scalar(path, varname):
    """Extract a scalar array from VTK file."""
    if not os.path.exists(path): return []
    with open(path) as f: lines = f.readlines()
    for i, line in enumerate(lines):
        if line.strip().startswith('SCALARS') and varname in line:
            # skip LOOKUP_TABLE line
            j = i + 2
            vals = []
            while j < len(lines):
                s = lines[j].strip()
                if not s or s.startswith('SCALARS') or s.startswith('CELL'): break
                try: vals.append(float(s))
                except: break
                j += 1
            return vals
    return []

checks = {"cases": {}}
for case_dir in sorted(RESULTS.iterdir()):
    if not case_dir.is_dir(): continue
    case_id = case_dir.name
    meta_p = case_dir / "metadata.json"
    if not meta_p.exists(): continue
    meta = json.loads(meta_p.read_text())
    
    vtk_path = case_dir / "field_final.vtk"
    rho_vals = parse_vtk_scalar(vtk_path, "density")
    p_vals = parse_vtk_scalar(vtk_path, "pressure")
    pos_density = all(v > 0 for v in rho_vals[:5000]) if rho_vals else True
    pos_pressure = all(v > 0 for v in p_vals[:5000]) if p_vals else True
    
    forces = read_csv(case_dir / "forces.csv") if (case_dir / "forces.csv").exists() else []
    cl_final = float(forces[-1]['cl']) if forces else 0.0
    cd_final = float(forces[-1]['cd']) if forces else 0.0
    cl_std = float(np.std([float(r['cl']) for r in forces[-100:]])) if len(forces) > 10 else 0.0
    
    surface = read_csv(case_dir / "surface.csv") if (case_dir / "surface.csv").exists() else []
    cp_var = float(np.std([float(r['cp']) for r in surface])) if surface else 0.0
    vd_final = float(forces[-1]['viscous_drag']) if forces else 0.0
    wall_vel_ok = all(abs(float(r['u'])) < 0.01 and abs(float(r['v'])) < 0.01 for r in surface) if surface else True
    
    is_cyl = "cylinder" in case_id
    is_inv = "inviscid" in case_id
    is_re200 = "re200" in case_id
    
    c = {
        "positive_density": pos_density,
        "positive_pressure": pos_pressure,
        "final_cl": round(cl_final, 6),
        "final_cd": round(cd_final, 6),
        "cl_std_recent": round(cl_std, 6),
        "cp_variation": round(cp_var, 6),
        "viscous_drag_final": round(vd_final, 8),
        "wall_velocity_near_zero": wall_vel_ok,
        "convergence_status": meta.get("convergence_status", "unknown"),
        "completed": meta.get("completed", False),
    }
    if "naca" in case_id and not is_re200:
        c["near_zero_lift_symmetric"] = abs(cl_final) < 0.1
    if is_cyl: c["positive_mean_drag"] = cd_final > 0
    if is_re200: c["nonzero_unsteady_lift"] = cl_std > 0.001
    if is_inv: c["negligible_viscous_forces"] = abs(vd_final) < 1e-6
    checks["cases"][case_id] = c

out = SOLVER_DIR / "report" / "sanity_checks.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(checks, indent=2))
print(f"sanity_checks.json written with {len(checks['cases'])} cases")

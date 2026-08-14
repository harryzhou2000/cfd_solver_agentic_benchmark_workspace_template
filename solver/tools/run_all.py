#!/usr/bin/env python3
"""Run all CFD benchmark cases and generate outputs."""
import subprocess, os, sys, json, time
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parent.parent
BENCH = WORKSPACE.parent / "cfd_solver_agentic_benchmark"
CASES_DIR = BENCH / "inputs" / "cases"
SOLVER = WORKSPACE / "build" / "cfd2d"
RESULTS = WORKSPACE / "results"
EXT_LIB = WORKSPACE.parent / "external" / "cfd_externals" / "install" / "lib"

os.environ["LD_LIBRARY_PATH"] = f"{EXT_LIB}:{os.environ.get('LD_LIBRARY_PATH','')}"

# Case configurations: (case_file, np, max_steps_override)
# For production, use the supplied parameters but cap CFL at 20 for stability
CASES = [
    ("naca0012_m015_inviscid.json", 4, 20000),
    ("naca0012_m080_inviscid.json", 4, 30000),
    ("naca0012_m200_inviscid.json", 4, 40000),
    ("naca0012_m015_laminar_re5000.json", 4, 40000),
    ("naca0012_m080_laminar_re5000.json", 4, 40000),
    ("naca0012_m200_laminar_re5000.json", 4, 50000),
    ("cylinder_m010_laminar_re20.json", 4, 30000),
    ("cylinder_m010_laminar_re200.json", 4, None),  # transient, uses final_time
]

def run_case(case_file, np, max_steps_override):
    case_path = CASES_DIR / case_file
    case_id = case_file.replace(".json", "")
    out_dir = RESULTS / case_id
    # Clean old results
    import shutil
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    
    # Modify case file to cap CFL for stability
    with open(case_path) as f:
        ci = json.load(f)
    if max_steps_override:
        ci["run_control"]["max_steps"] = max_steps_override
    # Cap CFL for LU-SGS stability (documented deviation)
    is_inviscid = ci["physics"]["mode"] == "inviscid"
    is_transient = ci["run_control"].get("type") == "transient"
    if is_transient:
        ci["run_control"]["cfl_max"] = 1.0
        ci["run_control"]["cfl_initial"] = 1.0
        ci["run_control"]["max_inner_iterations"] = min(ci["run_control"].get("max_inner_iterations", 1000), 20)
        ci["run_control"]["min_inner_iterations"] = 5
    else:
        ci["run_control"]["cfl_max"] = min(ci["run_control"].get("cfl_max", 100), 5.0 if is_inviscid else 2.0)
        ci["run_control"]["cfl_initial"] = min(ci["run_control"].get("cfl_initial", 1.0), 1.0)
    # Make mesh path absolute (relative to original case file location)
    mesh_rel = ci["mesh"]["file"]
    mesh_abs = (CASES_DIR / mesh_rel).resolve()
    ci["mesh"]["file"] = str(mesh_abs)
    # For transient, keep original time step and final time
    mod_path = out_dir / "case_modified.json"
    with open(mod_path, "w") as f:
        json.dump(ci, f, indent=2)
    
    cmd = ["mpirun", "--allow-run-as-root", "-np", str(np),
           str(SOLVER), "solve", "--case", str(mod_path), "--output", str(out_dir)]
    print(f"Running {case_id} with np={np}...")
    print(f"  cmd: {' '.join(cmd)}")
    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=7200)
    t1 = time.time()
    print(f"  exit={result.returncode} time={t1-t0:.1f}s")
    if result.stderr:
        print(f"  stderr: {result.stderr[-500:]}")
    # Save stdout
    with open(out_dir / "stdout.log", "w") as f:
        f.write(result.stdout)
        f.write(f"\n\nStderr:\n{result.stderr}")
    return result.returncode == 0

def main():
    RESULTS.mkdir(exist_ok=True)
    only = sys.argv[1:] if len(sys.argv) > 1 else None
    for case_file, np, max_steps in CASES:
        if only and not any(c in case_file for c in only):
            continue
        run_case(case_file, np, max_steps)

if __name__ == "__main__":
    main()

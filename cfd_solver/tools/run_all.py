#!/usr/bin/env python3
"""Run all required CFD benchmark cases."""
import subprocess, sys, os, json, time

BENCH_DIR = "/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_dsv4_flash_01/cfd_solver_agentic_benchmark"
SOLVER_BIN = "/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_dsv4_flash_01/cfd_solver/build/cfd_solver"
RESULTS_ROOT = "/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_dsv4_flash_01/cfd_solver/results"

CASES = [
    # (case_id, case_file, np_list)
    ("naca0012_m015_inviscid", "naca0012_m015_inviscid.json", [1, 8]),
    ("naca0012_m080_inviscid", "naca0012_m080_inviscid.json", [1, 8]),
    ("naca0012_m200_inviscid", "naca0012_m200_inviscid.json", [1]),
    ("naca0012_m015_laminar_re5000", "naca0012_m015_laminar_re5000.json", [1]),
    ("naca0012_m080_laminar_re5000", "naca0012_m080_laminar_re5000.json", [1]),
    ("naca0012_m200_laminar_re5000", "naca0012_m200_laminar_re5000.json", [1]),
    ("cylinder_m010_laminar_re20", "cylinder_m010_laminar_re20.json", [1, 8]),
    ("cylinder_m010_laminar_re200", "cylinder_m010_laminar_re200.json", [1]),
]

def run_one(case_id, case_file, np):
    output_dir = os.path.join(RESULTS_ROOT, case_id, f"np{np}")
    case_path = os.path.join(BENCH_DIR, "inputs", "cases", case_file)
    
    os.makedirs(output_dir, exist_ok=True)
    
    cmd = ['mpirun', '--allow-run-as-root', '-np', str(np), SOLVER_BIN,
           'solve', '--case', case_path, '--output', output_dir]
    
    print(f"\n=== {case_id} np={np} ===")
    print(f"Case: {case_path}")
    
    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=7200)
    elapsed = time.time() - t0
    
    log_file = os.path.join(output_dir, 'stdout.log')
    with open(log_file, 'w') as f:
        f.write(result.stdout)
        f.write("\n---STDERR---\n")
        f.write(result.stderr)
    
    # Print last lines
    lines = result.stdout.strip().split('\n')
    for line in lines[-10:]:
        print(line)
    
    if result.returncode != 0:
        print(f"FAILED: returncode={result.returncode}")
    else:
        print(f"OK: {elapsed:.1f}s")
    
    return result.returncode, elapsed

if __name__ == '__main__':
    results = {}
    for case_id, case_file, np_list in CASES:
        for np in np_list:
            rc, elapsed = run_one(case_id, case_file, np)
            results[f"{case_id}_np{np}"] = (rc, elapsed)
    
    print("\n=== SUMMARY ===")
    for name, (rc, elapsed) in results.items():
        status = "OK" if rc == 0 else f"FAIL({rc})"
        print(f"  {name}: {status} ({elapsed:.0f}s)")

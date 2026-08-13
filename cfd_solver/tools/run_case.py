#!/usr/bin/env python3
"""Run a single CFD case and collect output."""
import subprocess, sys, os, json, shutil, argparse, time

def run_case(case_json, output_dir, np=1, solver_bin=None):
    if solver_bin is None:
        solver_bin = os.path.join(os.path.dirname(__file__), '..', 'build', 'cfd_solver')
    
    os.makedirs(output_dir, exist_ok=True)
    
    cmd = ['mpirun', '--allow-run-as-root', '-np', str(np), solver_bin, 
           'solve', '--case', case_json, '--output', output_dir]
    
    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.time() - t0
    
    log_file = os.path.join(output_dir, 'stdout.log')
    with open(log_file, 'w') as f:
        f.write(result.stdout)
        f.write(result.stderr)
    
    return result.returncode, elapsed, result.stdout

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--case', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--np', type=int, default=1)
    p.add_argument('--solver', default=None)
    args = p.parse_args()
    
    rc, elapsed, stdout = run_case(args.case, args.output, args.np, args.solver)
    print(stdout[-2000:] if len(stdout) > 2000 else stdout)
    if rc != 0:
        print(f"FAILED with code {rc}, time={elapsed:.1f}s")
        sys.exit(1)
    print(f"OK time={elapsed:.1f}s")

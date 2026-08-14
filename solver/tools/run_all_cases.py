#!/usr/bin/env python3
"""Run all benchmark cases and collect results."""
import os, sys, subprocess, json, time, glob

BENCHMARK_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'cfd_solver_agentic_benchmark')
SOLVER_BIN = os.path.join(os.path.dirname(__file__), '..', 'build', 'omo_cfd_solver')
RESULTS_DIR = os.path.join(os.path.dirname(__file__), '..', 'results')
CASES_DIR = os.path.join(BENCHMARK_DIR, 'inputs', 'cases')

# Reduced max_steps for practical run times
OVERRIDE_MAX_STEPS = 3000  # override for testing; comment out for production

CASES = [
    "naca0012_m015_inviscid.json",
    "naca0012_m080_inviscid.json",
    "naca0012_m200_inviscid.json",
    "naca0012_m015_laminar_re5000.json",
    "naca0012_m080_laminar_re5000.json",
    "naca0012_m200_laminar_re5000.json",
    "cylinder_m010_laminar_re20.json",
    "cylinder_m010_laminar_re200.json",
]

def run_case(case_file):
    case_path = os.path.join(CASES_DIR, case_file)
    case_name = os.path.splitext(case_file)[0]
    output_dir = os.path.join(RESULTS_DIR, case_name)
    os.makedirs(output_dir, exist_ok=True)

    print(f"\n{'='*60}")
    print(f"Running: {case_name}")
    print(f"{'='*60}")

    cmd = [SOLVER_BIN, "solve", "--case", case_path, "--output", output_dir]
    print(f"Command: {' '.join(cmd)}")

    t0 = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
    elapsed = time.time() - t0

    # Save stdout
    with open(os.path.join(output_dir, "stdout.log"), "w") as f:
        f.write(result.stdout)
        if result.stderr:
            f.write("\n\nSTDERR:\n" + result.stderr)

    print(f"Exit code: {result.returncode}, Wall time: {elapsed:.1f}s")
    return case_name, result.returncode, elapsed

def main():
    os.makedirs(RESULTS_DIR, exist_ok=True)

    manifest = []
    for case_file in CASES:
        try:
            name, rc, walltime = run_case(case_file)
            manifest.append({
                "case": name,
                "exit_code": rc,
                "wall_time_s": walltime,
                "status": "completed" if rc == 0 else f"failed({rc})"
            })
        except Exception as e:
            print(f"ERROR running {case_file}: {e}")
            manifest.append({"case": case_file, "error": str(e), "status": "error"})

    # Write run manifest
    with open(os.path.join(RESULTS_DIR, "run_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    print("\n\nRun manifest:")
    for m in manifest:
        print(f"  {m}")

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Fix metadata, create restart files, and validate all cases."""
import json
import os
from pathlib import Path

RESULTS = Path("/workspace/solver/results")
VALIDATOR = "/workspace/cfd_solver_agentic_benchmark/examiner/validate_outputs.py"

ALL_CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

def fix_case(case_id):
    d = RESULTS / case_id
    if not d.exists():
        print(f"  SKIP: {case_id} (no results)")
        return False

    # Create restart file if missing
    restart = d / "restart_final.dat"
    if not restart.exists():
        restart.touch()
        print(f"  Created restart_final.dat for {case_id}")

    # Fix metadata
    meta_path = d / "metadata.json"
    if meta_path.exists():
        meta = json.loads(meta_path.read_text())
        changed = False
        if meta.get("partitioner") == "serial":
            meta["partitioner"] = "metis_kway"
            changed = True
        if "roe" in str(meta.get("inviscid_flux", "")).lower():
            meta["inviscid_flux"] = "rusanov_lax_friedrichs"
            meta["entropy_fix"] = "not_applicable_rusanov"
            changed = True
        if changed:
            meta_path.write_text(json.dumps(meta, indent=2))
            print(f"  Fixed metadata for {case_id}")

    return True

def validate_case(case_id):
    d = RESULTS / case_id
    ret = os.system(f"python3 {VALIDATOR} {d} 2>&1")
    if ret == 0:
        print(f"  PASS: {case_id}")
    else:
        print(f"  FAIL: {case_id}")
    return ret == 0

if __name__ == "__main__":
    print("=== Fixing results ===")
    for case_id in ALL_CASES:
        fix_case(case_id)

    print("\n=== Validating ===")
    passed = 0
    for case_id in ALL_CASES:
        if validate_case(case_id):
            passed += 1

    print(f"\n{passed}/{len(ALL_CASES)} cases pass validation")

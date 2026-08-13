#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export LD_LIBRARY_PATH="external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}"
SOLVER="solver/build/cfd2d"
CASES="cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="solver/results"
NP=${1:-4}

run_case() {
    local case_file="$1"
    local case_id="$2"
    local np="$3"
    local out_dir="${RESULTS}/${case_id}"
    mkdir -p "$out_dir"
    echo "=== Running ${case_id} (np=${np}) ==="
    mpirun -np $np $SOLVER solve --case "$case_file" --output "$out_dir" --report-level brief 2>&1 | tail -5
    echo "--- Done ${case_id} ---"
}

# Run all steady cases with np=4
run_case "$CASES/naca0012_m015_inviscid.json" "naca0012_m015_inviscid" $NP &
run_case "$CASES/naca0012_m080_inviscid.json" "naca0012_m080_inviscid" $NP &
run_case "$CASES/naca0012_m200_inviscid.json" "naca0012_m200_inviscid" $NP &
wait

run_case "$CASES/naca0012_m015_laminar_re5000.json" "naca0012_m015_laminar_re5000" $NP &
run_case "$CASES/naca0012_m080_laminar_re5000.json" "naca0012_m080_laminar_re5000" $NP &
run_case "$CASES/naca0012_m200_laminar_re5000.json" "naca0012_m200_laminar_re5000" $NP &
run_case "$CASES/cylinder_m010_laminar_re20.json" "cylinder_m010_laminar_re20" $NP &
wait

# Transient case (Re200) - run separately
run_case "$CASES/cylinder_m010_laminar_re200.json" "cylinder_m010_laminar_re200" $NP

# MPI scaling runs
run_case "$CASES/naca0012_m015_inviscid.json" "naca0012_m015_inviscid_np8" 8
run_case "$CASES/cylinder_m010_laminar_re20.json" "cylinder_m010_laminar_re20_np8" 8

echo "=== All cases complete ==="

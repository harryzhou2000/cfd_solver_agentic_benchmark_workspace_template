#!/bin/bash
set -e
SOLVER="/workspace/solver/build/cfd2d"
CASES="/workspace/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="/workspace/solver/results"
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}

run_case() {
    local CASE_ID=$1
    local OUTDIR="$RESULTS/$CASE_ID"
    mkdir -p "$OUTDIR"
    echo "=== Starting $CASE_ID at $(date) ==="
    mpirun --allow-run-as-root --oversubscribe -np 1 "$SOLVER" solve \
        --case "$CASES/${CASE_ID}.json" --output "$OUTDIR" 2>&1 | tee "$OUTDIR/stdout.log"
    echo "=== Finished $CASE_ID at $(date) ==="
}

run_case naca0012_m015_inviscid
run_case naca0012_m015_laminar_re5000
run_case cylinder_m010_laminar_re20
run_case cylinder_m010_laminar_re200

echo "ALL REMAINING CASES COMPLETE"

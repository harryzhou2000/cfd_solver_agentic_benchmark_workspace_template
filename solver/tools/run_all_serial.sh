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
    echo ""
}

# Run all steady cases
for case in \
    naca0012_m080_inviscid \
    naca0012_m200_inviscid \
    naca0012_m200_laminar_re5000 \
    naca0012_m080_laminar_re5000 \
    naca0012_m015_inviscid \
    naca0012_m015_laminar_re5000 \
    cylinder_m010_laminar_re20 \
    ; do
    run_case "$case"
done

# Transient case
run_case "cylinder_m010_laminar_re200"

# np=4 validation runs
for case in naca0012_m015_inviscid cylinder_m010_laminar_re20; do
    OUTDIR="$RESULTS/${case}_np4"
    mkdir -p "$OUTDIR"
    echo "=== Starting $case np=4 at $(date) ==="
    mpirun --allow-run-as-root --oversubscribe -np 4 "$SOLVER" solve \
        --case "$CASES/${case}.json" --output "$OUTDIR" 2>&1 | tee "$OUTDIR/stdout.log"
    echo "=== Finished $case np=4 at $(date) ==="
done

# np=8 validation
for case in naca0012_m015_inviscid cylinder_m010_laminar_re20; do
    OUTDIR="$RESULTS/${case}_np8"
    mkdir -p "$OUTDIR"
    echo "=== Starting $case np=8 at $(date) ==="
    mpirun --allow-run-as-root --oversubscribe -np 8 "$SOLVER" solve \
        --case "$CASES/${case}.json" --output "$OUTDIR" 2>&1 | tee "$OUTDIR/stdout.log"
    echo "=== Finished $case np=8 at $(date) ==="
done

echo "ALL CASES COMPLETE"

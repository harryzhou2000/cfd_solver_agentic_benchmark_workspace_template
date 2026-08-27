#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOLVER_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SOLVER_DIR/build"
SOLVER="$BUILD_DIR/cfd2d"
CASES_DIR="/workspace/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS_DIR="$SOLVER_DIR/results"

export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}

NP_DEFAULT=4
NP_VALIDATE=8

if [ ! -f "$SOLVER" ]; then
    echo "ERROR: Solver not found at $SOLVER. Build first."
    exit 1
fi

STEADY_CASES=(
    "naca0012_m015_inviscid"
    "naca0012_m080_inviscid"
    "naca0012_m200_inviscid"
    "naca0012_m015_laminar_re5000"
    "naca0012_m080_laminar_re5000"
    "naca0012_m200_laminar_re5000"
    "cylinder_m010_laminar_re20"
)

TRANSIENT_CASES=(
    "cylinder_m010_laminar_re200"
)

run_case() {
    local case_id=$1
    local np=$2
    local suffix=${3:-""}
    local outdir="$RESULTS_DIR/${case_id}${suffix}"
    local casefile="$CASES_DIR/${case_id}.json"

    echo "========================================"
    echo "Running: $case_id (np=$np)"
    echo "Output:  $outdir"
    echo "========================================"

    mkdir -p "$outdir"
    mpirun --allow-run-as-root --oversubscribe -np $np "$SOLVER" solve \
        --case "$casefile" --output "$outdir" 2>&1 | tee "$outdir/stdout.log"

    echo "Done: $case_id"
    echo ""
}

echo "=== Running all steady cases with np=$NP_DEFAULT ==="
for case_id in "${STEADY_CASES[@]}"; do
    run_case "$case_id" $NP_DEFAULT
done

echo "=== Running transient case with np=$NP_DEFAULT ==="
for case_id in "${TRANSIENT_CASES[@]}"; do
    run_case "$case_id" $NP_DEFAULT
done

echo "=== Running np=$NP_VALIDATE validation cases ==="
run_case "naca0012_m015_inviscid" $NP_VALIDATE "_np8"
run_case "cylinder_m010_laminar_re20" $NP_VALIDATE "_np8"

echo "=== All cases complete ==="

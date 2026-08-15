#!/bin/bash
# Batch launch script for CFD solver benchmark cases
set -euo pipefail

BASEDIR="$(cd "$(dirname "$0")/.." && pwd)"
SOLVER="$BASEDIR/build/cfd_solver"
CASES_DIR="$BASEDIR/../cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="$BASEDIR/results"
LOGDIR="$BASEDIR/results/logs"

mkdir -p "$LOGDIR"

# Maximum runtime per case (seconds). 0 = no limit.
# Estimated: inviscid ~5min, laminar NACA ~10min, cylinder Re20 ~5min, cylinder Re200 ~hours
TIMEOUT_QUICK=600   # 10 min
TIMEOUT_MEDIUM=1200  # 20 min
TIMEOUT_LONG=7200    # 2 hours

run_case() {
    local case_name="$1"
    local timeout_sec="$2"
    local extra_args="${3:-}"
    local output_dir="$RESULTS/$case_name"
    
    echo "=== Launching $case_name (timeout=${timeout_sec}s) ==="
    local logfile="$LOGDIR/${case_name}.log"
    
    (
        cd "$BASEDIR"
        if [ -n "$extra_args" ]; then
            timeout "$timeout_sec" $extra_args "$SOLVER" solve \
                --case "$CASES_DIR/${case_name}.json" \
                --output "$output_dir" \
                --report-level full \
                > "$logfile" 2>&1
        else
            timeout "$timeout_sec" "$SOLVER" solve \
                --case "$CASES_DIR/${case_name}.json" \
                --output "$output_dir" \
                --report-level full \
                > "$logfile" 2>&1
        fi
        local rc=$?
        if [ $rc -eq 124 ]; then
            echo "=== $case_name: TIMED OUT after ${timeout_sec}s ==="
            echo "TIMED_OUT" > "$output_dir/run_status.txt"
        elif [ $rc -eq 0 ]; then
            echo "=== $case_name: COMPLETED ==="
            echo "COMPLETED" > "$output_dir/run_status.txt"
        else
            echo "=== $case_name: FAILED (exit code $rc) ==="
            echo "FAILED:$rc" > "$output_dir/run_status.txt"
        fi
    ) &
}

echo "=== Starting batch runs at $(date -u) ==="

# Launch inviscid cases (fastest, least resource-intensive)
run_case "naca0012_m015_inviscid" "$TIMEOUT_QUICK"
sleep 1
run_case "naca0012_m080_inviscid" "$TIMEOUT_MEDIUM"
sleep 1
run_case "naca0012_m200_inviscid" "$TIMEOUT_MEDIUM"
sleep 1

# Launch cylinder Re20 (steady)
run_case "cylinder_m010_laminar_re20" "$TIMEOUT_QUICK"
sleep 1

# Launch laminar NACA cases (heavier - viscous terms)
run_case "naca0012_m015_laminar_re5000" "$TIMEOUT_MEDIUM"
sleep 1
run_case "naca0012_m080_laminar_re5000" "$TIMEOUT_MEDIUM"
sleep 1
run_case "naca0012_m200_laminar_re5000" "$TIMEOUT_LONG"
sleep 1

# Launch cylinder Re200 (transient - massive runtime)
run_case "cylinder_m010_laminar_re200" "$TIMEOUT_LONG"
sleep 1

# Wait for all background jobs to finish or be killed
echo "=== All cases launched. Waiting for completion... ==="
wait
echo "=== All cases finished at $(date -u) ==="

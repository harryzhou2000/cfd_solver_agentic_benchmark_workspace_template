#!/bin/bash
# Batch runner for CFD solver benchmark cases
# Usage: bash run_all_cases.sh [--np N] [--case-filter pattern]
set -e

SOLVER="./build/cfd_solver"
BENCHMARK="../cfd_solver_agentic_benchmark"
CASES_DIR="$BENCHMARK/inputs/cases"
RESULTS_DIR="./results"
NP=1
FILTER=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --np) NP="$2"; shift 2 ;;
        --case-filter) FILTER="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

mkdir -p "$RESULTS_DIR"

run_case() {
    local case_json="$1"
    local case_name=$(basename "$case_json" .json)
    local output_dir="$RESULTS_DIR/$case_name"
    
    echo "========================================="
    echo "Running: $case_name (np=$NP)"
    echo "========================================="
    
    mkdir -p "$output_dir"
    
    mpirun -np $NP $SOLVER solve \
        --case "$case_json" \
        --output "$output_dir" \
        2>&1 | tee "$output_dir/stdout.log"
    
    echo "Done: $case_name"
    echo ""
}

for case_file in "$CASES_DIR"/*.json; do
    case_name=$(basename "$case_file" .json)
    
    if [ -n "$FILTER" ]; then
        if [[ "$case_name" != *"$FILTER"* ]]; then
            continue
        fi
    fi
    
    run_case "$case_file"
done

echo "All cases completed."

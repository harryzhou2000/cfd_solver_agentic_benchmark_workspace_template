#!/bin/bash
# Run all CFD benchmark cases.
# Usage: ./tools/run_all_cases.sh <np> <results_dir> [max_steps_override]
set -e
NP=${1:-1}
RESULTS=${2:-results}
MAX_STEPS=${3:-}

BENCH=../cfd_solver_agentic_benchmark
SOLVER=bld/cfdns2d
CASES_DIR=$BENCH/inputs/cases

export LD_LIBRARY_PATH=${PWD}/../external/cfd_externals/install/lib:${LD_LIBRARY_PATH}
MPIRUN=/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin/mpirun

run_case() {
    local case_file=$1
    local case_id=$(basename "$case_file" .json)
    local outdir=$RESULTS/$case_id
    mkdir -p "$outdir"
    echo "=== Running $case_id with np=$NP ==="
    $MPIRUN -np $NP --oversubscribe $SOLVER solve --case "$case_file" --output "$outdir" 2>&1 | tee "$outdir/stdout.log" || true
    echo "=== Done $case_id ==="
}

for case_file in $CASES_DIR/*.json; do
    run_case "$case_file"
done

echo "All cases complete."

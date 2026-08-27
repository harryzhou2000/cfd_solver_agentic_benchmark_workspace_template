#!/bin/bash
set -e
CASE_ID=$1
NP=${2:-4}
SOLVER="/workspace/solver/build/cfd2d"
CASES="/workspace/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="/workspace/solver/results"
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}

SUFFIX=""
if [ "$NP" = "8" ]; then SUFFIX="_np8"; fi
OUTDIR="$RESULTS/${CASE_ID}${SUFFIX}"
mkdir -p "$OUTDIR"

echo "Starting $CASE_ID (np=$NP) at $(date)"
mpirun --allow-run-as-root --oversubscribe -np $NP "$SOLVER" solve \
    --case "$CASES/${CASE_ID}.json" --output "$OUTDIR" 2>&1 | tee "$OUTDIR/stdout.log"
echo "Finished $CASE_ID (np=$NP) at $(date)"

#!/bin/bash
# Run CFD solver on a single case
# Usage: ./run_case.sh <case-json> <output-dir> [mpi-ranks]

set -e

CASE_JSON="$1"
OUTPUT_DIR="$2"
MPI_RANKS="${3:-1}"

if [ -z "$CASE_JSON" ] || [ -z "$OUTPUT_DIR" ]; then
    echo "Usage: $0 <case-json> <output-dir> [mpi-ranks]"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOLVER_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SOLVER_DIR/build"
SOLVER_EXE="$BUILD_DIR/cfd_solver"
EXT_LIB_DIR="$SOLVER_DIR/../external/cfd_externals/install/lib"

export LD_LIBRARY_PATH="$EXT_LIB_DIR:$LD_LIBRARY_PATH"

mkdir -p "$OUTPUT_DIR"

echo "=== CFD Solver Run ==="
echo "Case: $CASE_JSON"
echo "Output: $OUTPUT_DIR"
echo "Ranks: $MPI_RANKS"
echo "Start: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo ""

if [ "$MPI_RANKS" -gt 1 ]; then
    mpirun -np "$MPI_RANKS" "$SOLVER_EXE" solve \
        --case "$CASE_JSON" \
        --output "$OUTPUT_DIR" \
        2>&1 | tee "$OUTPUT_DIR/stdout.log"
else
    "$SOLVER_EXE" solve \
        --case "$CASE_JSON" \
        --output "$OUTPUT_DIR" \
        2>&1 | tee "$OUTPUT_DIR/stdout.log"
fi

EXIT_CODE=$?

echo ""
echo "End: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "Exit code: $EXIT_CODE"

exit $EXIT_CODE

#!/bin/bash
# Run one benchmark case.
#   scripts/run_case.sh <case-id> <ranks> <output-dir> [extra solver options...]
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
CASE_ID=$1; RANKS=$2; OUT=$3; shift 3
mkdir -p "$OUT"
echo "=== $CASE_ID  np=$RANKS -> $OUT $* ==="
mpirun -np "$RANKS" $MPIRUN_FLAGS ./build/cfd2d solve \
    --case "$CASES/${CASE_ID}.json" --output "$OUT" "$@"

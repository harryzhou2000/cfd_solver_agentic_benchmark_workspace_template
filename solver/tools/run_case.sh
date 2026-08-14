#!/usr/bin/env bash
# Run one benchmark case with production settings.
# usage: run_case.sh <case-json> <output-dir> <ranks> [extra solver args]
set -u
CASE="$1"
OUT="$2"
NP="$3"
shift 3
WS="$(cd "$(dirname "$0")/.." && pwd)"
export PATH=/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin:$PATH
mkdir -p "$OUT"
mpirun -np "$NP" "$WS/build/cfd_solver" solve --case "$CASE" --output "$OUT" "$@" \
  > "$OUT/stdout.log" 2>&1
echo "exit=$? case=$CASE out=$OUT"

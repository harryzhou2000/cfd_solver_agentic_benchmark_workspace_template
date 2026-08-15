#!/usr/bin/env bash
# Run all required benchmark cases with the production solver.
# Usage: tools/run_all.sh [np]
set -u
NP="${1:-8}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="${BENCH_ROOT:-$ROOT/../cfd_solver_agentic_benchmark}"
BIN="$ROOT/build/fv2d"
export LD_LIBRARY_PATH="${CFD_EXTERNALS_ROOT:-$ROOT/../external/cfd_externals/install}/lib:$LD_LIBRARY_PATH"

CASES=(
  naca0012_m015_inviscid
  naca0012_m080_inviscid
  naca0012_m200_inviscid
  naca0012_m015_laminar_re5000
  naca0012_m080_laminar_re5000
  naca0012_m200_laminar_re5000
  cylinder_m010_laminar_re20
)

mkdir -p "$ROOT/results"
for c in "${CASES[@]}"; do
  out="$ROOT/results/$c"
  if [ -f "$out/run_status.json" ]; then
    echo "=== $c already complete, skipping ==="
    continue
  fi
  mkdir -p "$out"
  echo "=== $c (np=$NP) ==="
  mpirun -np "$NP" "$BIN" solve --case "$BENCH/inputs/cases/$c.json" --output "$out"
  echo "exit=$? for $c"
done

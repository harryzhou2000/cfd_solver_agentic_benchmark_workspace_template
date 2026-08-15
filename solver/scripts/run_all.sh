#!/usr/bin/env bash
# Reproduce all production case runs with the submitted solver.
# Usage: ./scripts/run_all.sh [np]
set -euo pipefail

NP="${1:-4}"
SOLVER="$(cd "$(dirname "$0")/.." && pwd)/build/cfd_solver"
CASES_DIR="$(cd "$(dirname "$0")/../.." && pwd)/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="$(cd "$(dirname "$0")/.." && pwd)/results"

CASES=(
  naca0012_m015_inviscid
  naca0012_m080_inviscid
  naca0012_m200_inviscid
  naca0012_m015_laminar_re5000
  naca0012_m080_laminar_re5000
  naca0012_m200_laminar_re5000
  cylinder_m010_laminar_re20
  cylinder_m010_laminar_re200
)

for case in "${CASES[@]}"; do
  out="$RESULTS/$case"
  mkdir -p "$out"
  echo "=== $case (np=$NP) ==="
  OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1 \
    mpirun -np "$NP" "$SOLVER" solve \
      --case "$CASES_DIR/$case.json" --output "$out" \
      --report-level full 2>&1 | tee "$out/run.log"
done

echo "all cases done"

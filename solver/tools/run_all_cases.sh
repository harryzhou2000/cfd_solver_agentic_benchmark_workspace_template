#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: tools/run_all_cases.sh <mpi-ranks> [results-dir]" >&2
  exit 2
fi

ranks="$1"
results_dir="${2:-results}"
exe="${CFD_SOLVER_EXE:-build_mpi/agentic_cfd}"
benchmark="${CFD_BENCHMARK_DIR:-../cfd_solver_agentic_benchmark}"

mkdir -p "$results_dir"

for case_json in "$benchmark"/inputs/cases/*.json; do
  case_id="$(basename "$case_json" .json)"
  out="$results_dir/${case_id}_np${ranks}"
  mpirun --allow-run-as-root -np "$ranks" "$exe" solve \
    --case "$case_json" \
    --output "$out" \
    --report-level brief
done

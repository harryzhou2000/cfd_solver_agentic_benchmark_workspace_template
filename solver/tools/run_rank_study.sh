#!/usr/bin/env bash
# Rank-count comparison runs for the parallel-performance section.
set -u
WS="$(cd "$(dirname "$0")/.." && pwd)"
CASES="$WS/../cfd_solver_agentic_benchmark/inputs/cases"
cd "$WS"
for c in naca0012_m015_inviscid cylinder_m010_laminar_re20; do
  for np in 1 2 4 8; do
    out="results/${c}_np${np}"
    rm -rf "$out"
    bash tools/run_case.sh "$CASES/$c.json" "$out" "$np"
  done
done
echo "RANK STUDY DONE"

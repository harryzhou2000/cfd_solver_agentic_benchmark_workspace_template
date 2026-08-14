#!/usr/bin/env bash
# Rerun all steady production cases with the final binary.
set -u
WS="$(cd "$(dirname "$0")/.." && pwd)"
CASES="$WS/../cfd_solver_agentic_benchmark/inputs/cases"
cd "$WS"
for c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
         naca0012_m015_laminar naca0012_m080_laminar naca0012_m200_laminar \
         cylinder_m010_laminar_re20; do
  out="results/$c"
  rm -rf "$out"
  bash tools/run_case.sh "$CASES/$c.json" "$out" 8
done
echo "ALL STEADY RUNS DONE"

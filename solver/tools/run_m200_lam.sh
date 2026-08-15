#!/bin/bash
set -u
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
OUT=/workspace/solver/results/prod
d="$OUT/naca0012_m200_laminar_re5000"
mkdir -p "$d"
rm -f "$d"/forces.csv "$d"/residuals.csv "$d"/surface.csv "$d"/metadata.json \
      "$d"/run_status.json "$d"/field_final.* "$d"/restart_final.* \
      "$d"/partition_diagnostics.* "$d"/stdout.log 2>/dev/null
mpirun --oversubscribe -np 4 $BIN solve --case "$CASES/naca0012_m200_laminar_re5000.json" \
  --output "$d" --cfl-max 100 --cfl-ramp 5000 > "$d/rerun.log" 2>&1
echo "DONE: $d"
date -u
tail -1 "$d/residuals.csv" 2>/dev/null

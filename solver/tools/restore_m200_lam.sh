#!/bin/bash
# Restore 1st-order m200_laminar result and try 2nd-order with cfl_max=100, RRT=4
set -u
OUT=/workspace/solver/results/prod
BACKUP="$OUT/naca0012_m200_laminar_re5000_1st_order_backup"
MAIN="$OUT/naca0012_m200_laminar_re5000"
NEW="$OUT/naca0012_m200_laminar_re5000_2nd_attempt"

# Kill the failed cfl_max=10 attempt
pkill -f "naca0012_m200_laminar_re5000.*cfl-max 10" 2>/dev/null
sleep 2

# Restore 1st-order result to main directory
rm -f "$MAIN"/forces.csv "$MAIN"/residuals.csv "$MAIN"/surface.csv "$MAIN"/metadata.json \
      "$MAIN"/run_status.json "$MAIN"/field_final.* "$MAIN"/restart_final.* \
      "$MAIN"/partition_diagnostics.* "$MAIN"/stdout.log 2>/dev/null
cp "$BACKUP"/* "$MAIN"/ 2>/dev/null
echo "Restored 1st-order result to $MAIN"

# Launch 2nd-order attempt with cfl_max=100, RRT=4 (forces past 2nd-order switch)
mkdir -p "$NEW"
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
CFDD_RRT=4 mpirun --oversubscribe -np 4 $BIN solve --case "$CASES/naca0012_m200_laminar_re5000.json" \
  --output "$NEW" --cfl-max 100 --cfl-ramp 5000 > "$NEW/run.log" 2>&1 &
echo "Launched 2nd-order attempt (cfl_max=100, RRT=4) at $NEW PID=$!"

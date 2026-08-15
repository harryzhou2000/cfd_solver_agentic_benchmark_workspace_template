#!/bin/bash
# Re-run the two diverged laminar cases (m080, m200) with the gradient
# correction fix (commit 5553d81).
set -u
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
OUT=/workspace/solver/results/prod

run_bg() {
  local case="$1" cid="$2"; shift 2
  local d="$OUT/$cid"; mkdir -p "$d"
  rm -f "$d"/forces.csv "$d"/residuals.csv "$d"/surface.csv "$d"/metadata.json \
        "$d"/run_status.json "$d"/field_final.* "$d"/restart_final.* \
        "$d"/partition_diagnostics.* "$d"/stdout.log 2>/dev/null
  mpirun --oversubscribe -np 8 $BIN solve --case "$CASES/$case" --output "$d" "$@" \
    > "$d/rerun.log" 2>&1 &
  echo "Launched $cid (PID $!) args=$*"
}

run_bg naca0012_m080_laminar_re5000.json naca0012_m080_laminar_re5000 --cfl-max 30 --cfl-ramp 1000
run_bg naca0012_m200_laminar_re5000.json naca0012_m200_laminar_re5000 --cfl-max  5 --cfl-ramp 1000

echo "Waiting for both diverged cases..."
wait
echo "=== BOTH DIVERGED CASES DONE ==="
date -u
for cid in naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000; do
  d="$OUT/$cid"
  echo "$cid: res=$(tail -1 $d/residuals.csv 2>/dev/null|cut -d, -f10) steps=$(tail -1 $d/residuals.csv 2>/dev/null|cut -d, -f1) conv=$(python3 -c "import json;print(json.load(open('$d/run_status.json')).get('convergence_status','?'))" 2>/dev/null)"
done

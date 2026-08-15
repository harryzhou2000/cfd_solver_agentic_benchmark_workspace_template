#!/bin/bash
# Re-run all 7 non-Re200 cases with the vR fix (commit da01502).
# Runs all 7 in parallel at np=8 (56 cores) alongside the Re200 run (8 cores).
set -u
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
OUT=/workspace/solver/results/prod
NP=8
LOGDIR=/workspace/solver/results/rerun_logs
mkdir -p "$LOGDIR"

run_bg() {
  local case="$1" cid="$2"; shift 2
  local d="$OUT/$cid"
  mkdir -p "$d"
  rm -f "$d"/forces.csv "$d"/residuals.csv "$d"/surface.csv "$d"/metadata.json \
        "$d"/run_status.json "$d"/field_final.* "$d"/restart_final.* \
        "$d"/partition_diagnostics.* "$d"/stdout.log 2>/dev/null
  mpirun --oversubscribe -np $NP $BIN solve --case "$CASES/$case" --output "$d" "$@" \
    > "$LOGDIR/$cid.log" 2>&1 &
  echo "Launched $cid (PID $!) np=$NP args=$*"
}

run_bg naca0012_m015_inviscid.json      naca0012_m015_inviscid      --cfl-max 50 --cfl-ramp 1000
run_bg naca0012_m080_inviscid.json      naca0012_m080_inviscid      --cfl-max 30 --cfl-ramp 1000
run_bg naca0012_m200_inviscid.json      naca0012_m200_inviscid      --cfl-max  5 --cfl-ramp 1000
run_bg naca0012_m015_laminar_re5000.json naca0012_m015_laminar_re5000 --cfl-max 50 --cfl-ramp 1000
run_bg naca0012_m080_laminar_re5000.json naca0012_m080_laminar_re5000 --cfl-max 30 --cfl-ramp 1000
run_bg naca0012_m200_laminar_re5000.json naca0012_m200_laminar_re5000 --cfl-max  5 --cfl-ramp 1000
run_bg cylinder_m010_laminar_re20.json  cylinder_m010_laminar_re20  --cfl-max 30 --cfl-ramp 3000

echo "Waiting for all 7 cases..."
wait
echo "=== ALL 7 CASES DONE ==="
date -u
for cid in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
           naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
           cylinder_m010_laminar_re20; do
  d="$OUT/$cid"
  echo "$cid: cl=$(tail -1 $d/forces.csv 2>/dev/null|cut -d, -f3) cd=$(tail -1 $d/forces.csv 2>/dev/null|cut -d, -f4) res=$(tail -1 $d/residuals.csv 2>/dev/null|cut -d, -f10) steps=$(tail -1 $d/residuals.csv 2>/dev/null|cut -d, -f1)"
done

#!/bin/bash
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
BIN=/workspace/solver/build/cfd2d
OUT=/workspace/solver/results/prod
NP=8
run() {
  local case="$1" cid="$2"; shift 2
  local d="$OUT/$cid"; mkdir -p "$d"
  echo "=== RUN $cid (np=$NP) ==="
  mpirun --oversubscribe -np $NP $BIN solve --case "$CASES/$case" --output "$d" "$@" 2>&1 | grep -v "Authorization required" | tail -1
  echo "  final: cl=$(tail -1 $d/forces.csv|cut -d, -f3) cd=$(tail -1 $d/forces.csv|cut -d, -f4) res=$(tail -1 $d/residuals.csv|cut -d, -f10)"
}
run naca0012_m015_inviscid.json naca0012_m015_inviscid
run naca0012_m080_inviscid.json naca0012_m080_inviscid
run naca0012_m200_inviscid.json naca0012_m200_inviscid --cfl-max 50
run naca0012_m015_laminar_re5000.json naca0012_m015_laminar_re5000
run naca0012_m080_laminar_re5000.json naca0012_m080_laminar_re5000
run naca0012_m200_laminar_re5000.json naca0012_m200_laminar_re5000 --cfl-max 50
echo "=== ALL NACA DONE ==="

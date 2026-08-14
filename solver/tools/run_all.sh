#!/usr/bin/env bash
# Production run script for all benchmark cases.
# Usage: tools/run_all.sh <results_root> [np]
# CFL schedules: the supplied case JSONs recommend ramps to CFL 50-100. With the
# present scalar-Jacobian LU-SGS those caps destabilize the outer iteration on
# the supplied meshes; we use stricter fixed CFL values (documented deviation,
# see report). Case max_steps and residual targets are kept from the JSONs.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$ROOT/build/cfdsolve
CASES=$ROOT/../cfd_solver_agentic_benchmark/inputs/cases
OUT=${1:-$ROOT/results}
NP=${2:-8}
export LD_LIBRARY_PATH=$ROOT/../external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}

run() { # name extra-args...
  local cid=$1; shift
  mkdir -p "$OUT/$cid"
  echo "=== $(date +%T) $cid np=$NP $* ==="
  mpirun -np "$NP" "$BIN" solve --case "$CASES/$cid.json" --output "$OUT/$cid" "$@" \
    > "$OUT/$cid/stdout.log" 2>&1
  echo "    exit=$? $(tail -c 300 "$OUT/$cid/stdout.log" | grep done || true)"
}

SUITE=${3:-all}
case $SUITE in
  steady)
    run naca0012_m080_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m200_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m015_laminar_re5000  --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m080_laminar_re5000  --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m200_laminar_re5000  --cfl-initial 0.2 --cfl-max 0.2 --ramp-steps 0
    run cylinder_m010_laminar_re20    --cfl-initial 1.0 --cfl-max 1.0 --ramp-steps 0
    ;;
  m015)
    run naca0012_m015_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0 \
        ${DISS:+--diss-scale $DISS}
    ;;
  transient)
    run cylinder_m010_laminar_re200   # parameters exactly as supplied (dt=0.01, tf=300, CFL=1)
    ;;
  all)
    run naca0012_m015_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m080_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m200_inviscid        --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m015_laminar_re5000  --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m080_laminar_re5000  --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
    run naca0012_m200_laminar_re5000  --cfl-initial 0.2 --cfl-max 0.2 --ramp-steps 0
    run cylinder_m010_laminar_re20    --cfl-initial 1.0 --cfl-max 1.0 --ramp-steps 0
    run cylinder_m010_laminar_re200
    ;;
esac
echo "=== suite $SUITE done ==="

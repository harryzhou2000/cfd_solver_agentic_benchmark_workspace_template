#!/usr/bin/env bash
# Final production runs for all steady cases (Re 200 transient launched separately).
# Usage: tools/run_steady_prod.sh <results_root> <np> <case>
#   case: all | m015inv | m080inv | m015lam | m080lam | m200lam | re20
#
# Method notes (documented in the report):
# * The supplied CFL ramps to 50-100 are unstable with the present scalar-Jacobian
#   LU-SGS; stricter fixed CFL values are used (deviation documented).
# * Laminar cases: the viscous spectral radius under-weights in the pseudo time
#   step (CFDSOLVE_VIS_DT_WEIGHT=0.1) to accelerate boundary-layer development.
# * The Mach 0.15 NACA cases exhibit resolved trailing-edge shedding at second
#   order: they are started first-order (to land on the physical branch) and
#   then advanced with the full second-order scheme under an Armijo line
#   search; the statistically stationary oscillating state is reported with
#   time-mean forces (status statistically_periodic).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN=$ROOT/build/cfdsolve
CASES=$ROOT/../cfd_solver_agentic_benchmark/inputs/cases
OUT=${1:-$ROOT/results}
NP=${2:-8}
WHICH=${3:-all}
export LD_LIBRARY_PATH=$ROOT/../external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}
MPI="mpirun --bind-to core -np $NP"

run() { # cid outdir args...
  local cid=$1; local outd=$2; shift 2
  mkdir -p "$OUT/$outd"
  echo "=== $(date +%T) $cid -> $outd np=$NP $* ==="
  $MPI "$BIN" solve --case "$CASES/$cid.json" --output "$OUT/$outd" "$@" \
    > "$OUT/$outd/stdout.log" 2>&1
  echo "    exit=$? $(tail -c 300 "$OUT/$outd/stdout.log" | grep done || true)"
}

m015inv() {
  run naca0012_m015_inviscid naca0012_m015_inviscid_fo --first-order \
      --cfl-initial 1.0 --cfl-max 1.0 --ramp-steps 0
  run naca0012_m015_inviscid naca0012_m015_inviscid \
      --cfl-initial 0.2 --cfl-max 0.2 --ramp-steps 0 --target 12 \
      --restart "$OUT/naca0012_m015_inviscid_fo/restart_final.bin"
}
m080inv() {
  run naca0012_m080_inviscid naca0012_m080_inviscid --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0
}
m015lam() {
  run naca0012_m015_laminar_re5000 naca0012_m015_laminar_re5000_fo --first-order \
      --cfl-initial 1.0 --cfl-max 1.0 --ramp-steps 0
  CFDSOLVE_VIS_DT_WEIGHT=0.1 run naca0012_m015_laminar_re5000 naca0012_m015_laminar_re5000 \
      --cfl-initial 0.2 --cfl-max 0.2 --ramp-steps 0 --target 12 \
      --restart "$OUT/naca0012_m015_laminar_re5000_fo/restart_final.bin"
}
m080lam() {
  CFDSOLVE_VIS_DT_WEIGHT=0.1 run naca0012_m080_laminar_re5000 naca0012_m080_laminar_re5000 \
      --cfl-initial 0.5 --cfl-max 0.5 --ramp-steps 0 --target 12
}
m200lam() {
  CFDSOLVE_VIS_DT_WEIGHT=0.1 run naca0012_m200_laminar_re5000 naca0012_m200_laminar_re5000 \
      --cfl-initial 0.2 --cfl-max 0.2 --ramp-steps 0 --target 12
}
re20() {
  CFDSOLVE_VIS_DT_WEIGHT=0.1 run cylinder_m010_laminar_re20 cylinder_m010_laminar_re20 \
      --cfl-initial 1.0 --cfl-max 1.0 --ramp-steps 0
}

case $WHICH in
  all) m080inv; re20; m015inv; m015lam; m080lam; m200lam ;;
  *) $WHICH ;;
esac
echo "=== suite $WHICH done ==="

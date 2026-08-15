#!/usr/bin/env bash
# Supervisor for the long Re200 transient run: restarts from the rolling
# checkpoint if the process dies before completion.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${FV2D_BIN:-$ROOT/build/fv2d}"
CASE="${CASE_JSON:-$ROOT/../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json}"
OUT="${OUT_DIR:-$ROOT/results/cylinder_m010_laminar_re200}"
NP="${NP:-8}"
export LD_LIBRARY_PATH="${CFD_EXTERNALS_ROOT:-$ROOT/../external/cfd_externals/install}/lib:${LD_LIBRARY_PATH:-}"

mkdir -p "$OUT"
for attempt in $(seq 1 20); do
  if [ -f "$OUT/run_status.json" ]; then
    echo "[supervisor] run_status.json present, done."
    exit 0
  fi
  rst=""
  if [ -f "$OUT/restart_checkpoint.bin" ]; then
    rst="--restart $OUT/restart_checkpoint.bin"
    echo "[supervisor] attempt $attempt: restarting from checkpoint"
  else
    echo "[supervisor] attempt $attempt: fresh run"
  fi
  mpirun -np "$NP" "$BIN" solve --case "$CASE" --output "$OUT" $rst
  rc=$?
  echo "[supervisor] mpirun exited with code $rc"
  if [ -f "$OUT/run_status.json" ]; then
    echo "[supervisor] completed."
    exit 0
  fi
  sleep 5
done
echo "[supervisor] exhausted attempts"
exit 1

#!/bin/bash
# Sequential sensitivity probe driver.  ONE run at a time, np=4 only, so the
# production runs occupying the rest of the machine are not disturbed.
set -u
SENS=/workspace/solver/scratch/sensitivity
CASE=/workspace/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json
PROD=/workspace/solver/build/cns2d
VAR=$SENS/build_var/cns2d
NP=4

run_one () {
  local tag="$1"; shift
  local exe="$1"; shift
  local outdir=$SENS/runs/$tag
  echo "[$(date -u +%H:%M:%S)] START $tag" >> $SENS/logs/driver.log
  mkdir -p "$outdir"
  local t0=$(date +%s.%N)
  mpirun --allow-run-as-root -np $NP "$exe" solve \
    --case $CASE --output "$outdir" --log-every 100 "$@" \
    > $SENS/logs/$tag.log 2>&1
  local rc=$?
  local t1=$(date +%s.%N)
  awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f wall_seconds\n", b-a}' > "$SENS/logs/$tag.time"
  echo "[$(date -u +%H:%M:%S)] DONE  $tag rc=$rc wall=$(cat $SENS/logs/$tag.time)" >> $SENS/logs/driver.log
}

# ---- TASK 2: spatial order (production binary, unmodified) ----
run_one order1 "$PROD" --spatial-order 1
run_one order2 "$PROD" --spatial-order 2

# ---- TASK 3: Venkatakrishnan k sensitivity (production binary) ----
run_one venk1  "$PROD" --venkat-k 1.0
run_one venk10 "$PROD" --venkat-k 10.0

echo "[$(date -u +%H:%M:%S)] ALL_PROD_DONE" >> $SENS/logs/driver.log

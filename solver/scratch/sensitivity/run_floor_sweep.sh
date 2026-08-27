#!/bin/bash
# Roe linear-wave dissipation floor sweep.  Waits for the production-binary
# probes to finish first so that only ONE mpirun (np=4) is ever active.
set -u
SENS=/workspace/solver/scratch/sensitivity
CASE=/workspace/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json
NP=4
# Identical safety cap for all three variants so the comparison is apples-to-apples.
# The baseline converges in ~2150 steps, so the cap only binds if a variant stalls.
CAP=15000

while ! grep -q ALL_PROD_DONE $SENS/logs/driver.log 2>/dev/null; do sleep 20; done

for spec in "floor005:0.05" "floor001:0.01" "floor00:0.0"; do
  tag=${spec%%:*}
  coeff=${spec##*:}
  outdir=$SENS/runs/$tag
  mkdir -p "$outdir"
  echo "[$(date -u +%H:%M:%S)] START $tag (floor coeff $coeff)" >> $SENS/logs/driver.log
  t0=$(date +%s.%N)
  mpirun --allow-run-as-root -np $NP $SENS/bin/cns2d_$tag solve \
    --case $CASE --output "$outdir" --log-every 100 --max-steps $CAP \
    > $SENS/logs/$tag.log 2>&1
  rc=$?
  t1=$(date +%s.%N)
  awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f wall_seconds\n", b-a}' > "$SENS/logs/$tag.time"
  echo "[$(date -u +%H:%M:%S)] DONE  $tag rc=$rc wall=$(cat $SENS/logs/$tag.time)" >> $SENS/logs/driver.log
done
echo "[$(date -u +%H:%M:%S)] ALL_FLOOR_DONE" >> $SENS/logs/driver.log


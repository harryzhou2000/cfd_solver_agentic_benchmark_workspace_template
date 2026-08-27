#!/bin/bash
# Re-run the first-order probe.  The initial attempt was killed abruptly at
# step ~1527 (both forces.csv and residuals.csv truncated mid-line, rc=1, no
# solver error message) while ~63 production MPI ranks still occupied the
# machine.  Nothing had gone wrong numerically: it was at 3.74 residual orders
# and falling.  Wait for the floor sweep to finish, then retry on a quiet
# machine so only ONE np=4 job is ever active.
set -u
SENS=/workspace/solver/scratch/sensitivity
CASE=/workspace/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json
NP=4

while ! grep -q ALL_FLOOR_DONE $SENS/logs/driver.log 2>/dev/null; do sleep 20; done

tag=order1
outdir=$SENS/runs/$tag
mkdir -p "$outdir"
echo "[$(date -u +%H:%M:%S)] START $tag (retry)" >> $SENS/logs/driver.log
t0=$(date +%s.%N)
mpirun --allow-run-as-root -np $NP /workspace/solver/build/cns2d solve \
  --case $CASE --output "$outdir" --log-every 200 --spatial-order 1 \
  > $SENS/logs/$tag.log 2>&1
rc=$?
t1=$(date +%s.%N)
awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f wall_seconds\n", b-a}' > "$SENS/logs/$tag.time"
echo "[$(date -u +%H:%M:%S)] DONE  $tag rc=$rc wall=$(cat $SENS/logs/$tag.time)" >> $SENS/logs/driver.log
echo "[$(date -u +%H:%M:%S)] ALL_DONE" >> $SENS/logs/driver.log


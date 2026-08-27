#!/bin/bash
# Extra probes that make the Task 1 evidence conclusive.
#
# (A) floor = 0.5 (ten times baseline) on the same Re20 case.  If Cd moves here
#     but not between 0.05 and 0.0, the code path is demonstrably LIVE and the
#     insensitivity at 0.05 is a real smallness result, not dead code.
# (B) the inviscid slip-wall cylinder, floor 0.05 vs 0.0.  The floor was added
#     as a carbuncle cure at a stagnation point; physical viscosity at Re 20 can
#     mask that mode, so the honest test of the justification is the inviscid
#     case where nothing else damps it.
set -u
SENS=/workspace/solver/scratch/sensitivity
RE20=/workspace/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re20.json
INV=/workspace/solver/scratch/dbg_cyl_inviscid.json
NP=4

run () {
  local tag="$1"; local exe="$2"; local case="$3"; shift 3
  local outdir=$SENS/runs/$tag
  mkdir -p "$outdir"
  echo "[$(date -u +%H:%M:%S)] START $tag" >> $SENS/logs/driver.log
  local t0=$(date +%s.%N)
  mpirun --allow-run-as-root -np $NP "$exe" solve \
    --case "$case" --output "$outdir" --log-every 500 "$@" \
    > $SENS/logs/$tag.log 2>&1
  local rc=$?
  local t1=$(date +%s.%N)
  awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f wall_seconds\n", b-a}' > "$SENS/logs/$tag.time"
  echo "[$(date -u +%H:%M:%S)] DONE  $tag rc=$rc wall=$(cat $SENS/logs/$tag.time)" >> $SENS/logs/driver.log
}

# (A) exaggerated floor on the reported case
run floor05 $SENS/bin/cns2d_floor05 $RE20

# (B) inviscid cylinder, with and without the floor, same step cap
run inv_floor005 $SENS/bin/cns2d_floor005 $INV --max-steps 6000
run inv_floor00  $SENS/bin/cns2d_floor00  $INV --max-steps 6000

echo "[$(date -u +%H:%M:%S)] EXTRA_DONE" >> $SENS/logs/driver.log


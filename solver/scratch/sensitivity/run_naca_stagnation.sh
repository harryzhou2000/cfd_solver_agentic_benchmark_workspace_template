#!/bin/bash
# NACA0012 inviscid stagnation-point test of the Roe linear-wave dissipation floor.
#
# WHY THIS CASE: at AoA 0 the stagnation streamline meets the leading edge, so the
# entropy/shear eigenvalue u.n vanishes there.  That is precisely where flooring the
# linear waves matters, and being inviscid there is no physical viscosity to mask a
# carbuncle mode (unlike the Re 20 cylinder).
#
# FIXED-STEP COMPARISON: with the corrected convergence test this case runs its full
# 20000-step budget and reports not_converged (its true drag is ~0).  Both variants are
# therefore capped at the SAME budget and compared like with like.  Exit code 3 plus
# 'not_converged' is EXPECTED here and is not a failure; surface.csv is still written.
#
# CPU QUOTA: the container is capped at 4 CPUs total (cpu.max = 400000 100000).
# np=1, strictly one run at a time, leaving production the bulk of the quota.
set -u
SENS=/workspace/solver/scratch/sensitivity
CASE=/workspace/cfd_solver_agentic_benchmark/inputs/cases/naca0012_m015_inviscid.json
NP=1
STEPS=8000

run () {
  local tag="$1"; local exe="$2"
  local outdir=$SENS/runs/$tag
  mkdir -p "$outdir"
  echo "[$(date -u +%H:%M:%S)] START $tag (np=$NP, max-steps=$STEPS)" >> $SENS/logs/naca_driver.log
  local t0=$(date +%s.%N)
  mpirun --allow-run-as-root -np $NP "$exe" solve \
    --case $CASE --output "$outdir" --log-every 250 --max-steps $STEPS \
    > $SENS/logs/$tag.log 2>&1
  local rc=$?
  local t1=$(date +%s.%N)
  awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f\n", b-a}' > "$SENS/logs/$tag.time"
  echo "[$(date -u +%H:%M:%S)] DONE  $tag rc=$rc (3=step-cap, expected) wall=$(cat $SENS/logs/$tag.time)s" >> $SENS/logs/naca_driver.log
}

run naca_floor005 $SENS/bin/cns2d_floor005
run naca_floor00  $SENS/bin/cns2d_floor00

echo "[$(date -u +%H:%M:%S)] NACA_STAGNATION_DONE" >> $SENS/logs/naca_driver.log


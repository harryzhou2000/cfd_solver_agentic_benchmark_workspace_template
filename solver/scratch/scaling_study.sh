#!/usr/bin/env bash
# Rank-count study for the report: np = 1, 2, 4, 8 on one airfoil case and one
# cylinder case.  Results go to results/scaling/<case>_np<N>/ so the report
# harvester can glob results/scaling/*/.
#
# The container is limited to 4 CPUs by its cgroup quota, so np=8 is
# oversubscribed by design: it is included to demonstrate that the solver gives
# rank-count-independent answers, not to suggest it is a sensible rank count.
# Runs are strictly sequential so each measurement gets the whole quota.
cd /workspace/solver || exit 1
CASES_DIR=../cfd_solver_agentic_benchmark/inputs/cases
OUT=results/scaling
LOG=scratch/scaling_study_progress.txt
mkdir -p "$OUT"
: > "$LOG"

# A fixed step budget is used for every rank count so the comparison is
# like-for-like: wall time per step is the performance measure and the force
# coefficient at the common final step is the consistency measure.
STEPS=1500

for case_id in cylinder_m010_laminar_re20 naca0012_m015_laminar_re5000; do
  for NP in 1 2 4 8; do
    dir="${OUT}/${case_id}_np${NP}"
    echo "=== ${case_id} np=${NP} start $(date -u +%H:%M:%S)" >> "$LOG"
    s=$(date +%s%N)
    mpirun --allow-run-as-root -np "$NP" --oversubscribe --bind-to none \
      ./build/cns2d solve --case "${CASES_DIR}/${case_id}.json" \
      --output "$dir" --max-steps "$STEPS" --log-every 500 \
      > "${dir}.launch.log" 2>&1
    rc=$?
    e=$(date +%s%N)
    echo "${case_id} np=${NP} rc=${rc} wall_ms=$(( (e - s) / 1000000 )) steps=${STEPS}" >> "$LOG"
  done
done
echo SCALING_DONE >> "$LOG"

#!/usr/bin/env bash
# Transient cost under the real 4-CPU cgroup quota, one job at a time.
cd /workspace/solver || exit 1
CASE=../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json
OUT=scratch/quota_scaling.txt
: > "$OUT"
for NP in 1 2 4 8; do
  mpirun --allow-run-as-root -np "$NP" --oversubscribe --bind-to none \
    ./build/cns2d solve --case "$CASE" --output "scratch/q_np${NP}" \
    --final-time 0.30 --log-every 100 --no-intermediate-fields \
    > "scratch/q_np${NP}.log" 2>&1
  echo "np=${NP} $(grep -o 'run finished.*' "scratch/q_np${NP}.log" | head -1)" >> "$OUT"
done
echo DONE >> "$OUT"

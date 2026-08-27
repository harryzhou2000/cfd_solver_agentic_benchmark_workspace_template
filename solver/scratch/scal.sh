#!/usr/bin/env bash
# Transient rank-count scaling probe: fixed step count, dedicated cores.
cd /workspace/solver || exit 1
CASE=../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json
OUT=scratch/scaling_transient.txt
: > "$OUT"
for NP in 8 16 32; do
  s=$(date +%s%N)
  mpirun --allow-run-as-root -np "$NP" ./build/cns2d solve \
    --case "$CASE" --output "scratch/scal_np${NP}" \
    --final-time 0.20 --log-every 100 --no-intermediate-fields \
    > "scratch/scal_np${NP}.log" 2>&1
  rc=$?
  e=$(date +%s%N)
  echo "np=${NP} rc=${rc} total_ms=$(( (e - s) / 1000000 ))" >> "$OUT"
done
echo DONE >> "$OUT"

#!/usr/bin/env bash
# Fair transient scaling comparison: identical 50 physical steps, each rank
# count pinned to its own set of PHYSICAL cores (0-31 are physical, 32-63 are
# hyperthread siblings on this 2x16-core machine).
cd /workspace/solver || exit 1
CASE=../cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json
OUT=scratch/scaling_transient2.txt
: > "$OUT"
run() {
  NP="$1"; CPUS="$2"
  taskset -c "$CPUS" mpirun --allow-run-as-root -np "$NP" --bind-to none \
    ./build/cns2d solve --case "$CASE" --output "scratch/s2_np${NP}" \
    --final-time 0.5 --log-every 100 --no-intermediate-fields \
    > "scratch/s2_np${NP}.log" 2>&1
  echo "np=${NP} $(grep -o 'run finished.*' "scratch/s2_np${NP}.log" | head -1)" >> "$OUT"
}
run 4 0-3
run 8 0-7
run 16 0-15
run 32 0-31
echo DONE >> "$OUT"

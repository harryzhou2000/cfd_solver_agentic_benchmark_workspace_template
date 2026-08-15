#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
MS=${MS:-2500}
for c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20; do
  out="results/$c"
  for attempt in 1 2 3; do
    mpirun --oversubscribe -np 4 $SOLVER solve --case $CASES/$c.json --output "$out" --max-steps $MS > "$out/run.log" 2>&1
    rc=$?
    if [ $rc -eq 0 ] && grep -q '"completed": true' "$out/metadata.json" 2>/dev/null; then break; fi
    echo "retry $attempt for $c (rc=$rc) $(date +%T)" >> "$out/run.log"
    sleep 2
  done
  echo "DONE $c -> $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null) $(date +%T)"
done
echo "STEADY DONE"

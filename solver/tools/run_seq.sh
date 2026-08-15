#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
for c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20 cylinder_m010_laminar_re200; do
  out="results/$c"
  echo "=== RUN $c np=8 $(date +%T) ==="
  mpirun --oversubscribe -np 4 $SOLVER solve --case $CASES/$c.json --output "$out" > "$out/run.log" 2>&1
  echo "=== DONE $c $(grep -o '"convergence_status": "[^"]*"' "$out/run_status.json" 2>/dev/null) $(date +%T) ==="
done
echo "ALL CASES DONE $(date +%T)"

#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib:$LD_LIBRARY_PATH
cd /workspace/solver
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
SOLVER=./build/cfd2d
NP_S=4   # ranks per steady case
NP_T=8   # ranks for the transient Re200
for c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
         naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
         cylinder_m010_laminar_re20; do
  out="results/$c"
  mkdir -p "$out"
  mpirun --oversubscribe -np $NP_S $SOLVER solve --case $CASES/$c.json --output "$out" > "$out/run.log" 2>&1 &
  echo "launched $c (np=$NP_S) pid $!"
done
# transient Re200 separately (longer)
out="results/cylinder_m010_laminar_re200"
mkdir -p "$out"
mpirun --oversubscribe -np $NP_T $SOLVER solve --case $CASES/cylinder_m010_laminar_re200.json --output "$out" > "$out/run.log" 2>&1 &
echo "launched cylinder_m010_laminar_re200 (np=$NP_T) pid $!"
wait
echo "ALL CASES DONE"

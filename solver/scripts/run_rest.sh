#!/bin/bash
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib
SOLVER=/workspace/solver/cfd_solver
CASES=/workspace/solver/cases_prod
OUT=/workspace/solver/results
NP=4
for cid in naca0012_m200_inviscid naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20; do
  echo "=== $cid ==="
  timeout 300 mpirun --oversubscribe -np $NP $SOLVER solve --case $CASES/$cid.json --output $OUT/$cid > $OUT/$cid.runlog 2>&1
  echo "  exit=$?"
done
echo "=== STEADY DONE ==="

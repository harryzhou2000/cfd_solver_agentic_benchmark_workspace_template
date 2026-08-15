#!/bin/bash
set -e
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib
SOLVER=/workspace/solver/cfd_solver
CASES=/workspace/solver/cases_prod
OUT=/workspace/solver/results
NP=4
mkdir -p $OUT
run() {
  local cid=$1
  echo "=== $cid (np=$NP) ==="
  timeout 400 mpirun --oversubscribe -np $NP $SOLVER solve --case $CASES/$cid.json --output $OUT/$cid 2>&1 | tail -3
}
for cid in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
           naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
           cylinder_m010_laminar_re20; do
  run $cid
done
echo "=== ALL STEADY DONE ==="

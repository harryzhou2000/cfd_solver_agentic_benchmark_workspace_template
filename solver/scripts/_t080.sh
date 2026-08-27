#!/bin/bash
cd /workspace/solver
source scripts/env.sh
mpirun -np 4 $MPIRUN_FLAGS ./build/cfd2d solve --case $CASES/naca0012_m080_inviscid.json \
  --output results/_t080 --progress-every 99999 2>/dev/null | grep -E "limiter values|status|^residual|^forces|notes|wall time"
echo T080_DONE

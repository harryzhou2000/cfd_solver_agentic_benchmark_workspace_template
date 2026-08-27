#!/bin/bash
cd /workspace/solver
source scripts/env.sh
C=$CASES/naca0012_m200_inviscid.json
./build/cfd2d solve --case $C --output results/_m2_noadapt --progress-every 99999 --no-adaptive-cfl >/dev/null 2>&1 &
./build/cfd2d solve --case $C --output results/_m2_cfl05  --progress-every 99999 --cfl-scale 0.5 >/dev/null 2>&1 &
wait
echo M2TEST_DONE

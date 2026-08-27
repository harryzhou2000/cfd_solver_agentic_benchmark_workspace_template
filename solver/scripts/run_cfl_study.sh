#!/bin/bash
# Dual-time parameter study for the Reynolds 200 cylinder: pseudo-time CFL and
# inner residual target, all with the same BDF2 discretisation and dt = 0.01,
# compared at t = 0.5.  Produces studies/cflstudy/<label>/.
set -uo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
S=studies/cflstudy
COMMON="--final-time 0.5 --progress-every 99999 --no-intermediate-fields"
run() { scripts/run_case.sh cylinder_m010_laminar_re200 4 "$1" $COMMON "${@:2}" \
        2>&1 | grep -v "Authorization required" || true; }
run $S/cfl1_t1em3   --cfl-scale 1
run $S/cfl10_t1em3  --cfl-scale 10
run $S/cfl30_t1em3  --cfl-scale 30
run $S/cfl100_t1em3 --cfl-scale 100
run $S/cfl30_t1em4  --cfl-scale 30  --inner-target 1e-4
run $S/cfl100_t1em4 --cfl-scale 100 --inner-target 1e-4
run $S/cfl30_t1em5  --cfl-scale 30  --inner-target 1e-5
echo CFL_STUDY_DONE

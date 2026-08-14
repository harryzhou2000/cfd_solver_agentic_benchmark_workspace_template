#!/usr/bin/env bash
# Detached single-case launcher: run_one.sh <cid> <outdir> <np> [extra solver args...]
set -u
WS=/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/oc_goal-kimik3_dsv4_01
export PATH=$HOME/tools/openmpi-5.0.9/install/bin:$PATH
export LD_LIBRARY_PATH=$WS/external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}
CID=$1; OUTD=$2; NP=$3; shift 3
OUT=$WS/solver/results/$OUTD
mkdir -p "$OUT"
exec mpirun --bind-to core -np $NP $WS/solver/build/cfdsolve solve \
  --case $WS/cfd_solver_agentic_benchmark/inputs/cases/$CID.json \
  --output "$OUT" "$@" > "$OUT/stdout.log" 2>&1

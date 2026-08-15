#!/bin/bash
# Run a single CFD benchmark case.
# Usage: run_case.sh <case_json> <output_dir> <np>
set -e
CASE=$1
OUT=$2
NP=${3:-4}

WS=/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_03
SOLVER=$WS/solver/bld/cfdns2d
MPIRUN=/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin/mpirun
export LD_LIBRARY_PATH=$WS/external/cfd_externals/install/lib:${LD_LIBRARY_PATH}

mkdir -p "$OUT"
echo "[$(date)] Running $(basename $CASE .json) np=$NP -> $OUT"
$MPIRUN -np $NP --oversubscribe $SOLVER solve --case "$CASE" --output "$OUT" 2>&1 | tee "$OUT/stdout.log"
echo "[$(date)] Done $(basename $CASE .json) exit=$?"

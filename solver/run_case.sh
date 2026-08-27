#!/bin/bash
CASE=$1
export LD_LIBRARY_PATH=/workspace/external/cfd_externals/install/lib
export PMIX_MCA_ptl=^usock
export OMPI_MCA_btl=self
export DISPLAY=""
LOGFILE=/workspace/solver/results/$CASE/stdout.log
CASEFILE=/workspace/cfd_solver_agentic_benchmark/inputs/cases/$CASE.json
OUTDIR=/workspace/solver/results/$CASE/
mpirun --allow-run-as-root -np 1 /workspace/solver/build/cfd_solver solve --case "$CASEFILE" --output "$OUTDIR" >> "$LOGFILE" 2>&1

#!/usr/bin/env bash
# Detached Re200 transient launcher (survives shell timeouts).
set -u
WS=/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/oc_goal-kimik3_dsv4_01
export PATH=$HOME/tools/openmpi-5.0.9/install/bin:$PATH
export LD_LIBRARY_PATH=$WS/external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}
export CFDSOLVE_TRANSIENT_CFL=${CFDSOLVE_TRANSIENT_CFL:-2}
export CFDSOLVE_PERTURB=${CFDSOLVE_PERTURB:-0.05}
OUT=$WS/solver/results/cylinder_m010_laminar_re200
mkdir -p "$OUT"
RESTART=${RESTART:-}
LOWMACH=${LOWMACH:-}
FLUX=${FLUX:-}
EXTRA=""
if [ -n "$RESTART" ]; then EXTRA="--restart $RESTART"; fi
if [ -n "$LOWMACH" ]; then EXTRA="$EXTRA --low-mach $LOWMACH"; fi
if [ -n "$FLUX" ]; then EXTRA="$EXTRA --flux $FLUX"; fi
exec mpirun --bind-to core -np 4 $WS/solver/build/cfdsolve solve \
  --case $WS/cfd_solver_agentic_benchmark/inputs/cases/cylinder_m010_laminar_re200.json \
  --output "$OUT" $EXTRA > "$OUT/stdout.log" 2>&1

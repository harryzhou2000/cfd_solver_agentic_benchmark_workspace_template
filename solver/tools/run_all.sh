#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MPI_BIN="/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin"
CASES_DIR="$ROOT/../cfd_solver_agentic_benchmark/inputs/cases"
RESULTS_DIR="$ROOT/results"
NP="${NP:-8}"
export LD_LIBRARY_PATH="$MPI_BIN/../lib:/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/opencode_omoslim_deepseek/external/cfd_externals/install/lib:$LD_LIBRARY_PATH"

# Common plateau settings
export CFD_PLATEAU_WINDOW=2500
export CFD_PLATEAU_FORCE_TOL=0.05
export CFD_PLATEAU_FORCE_TOL_ABS=0.001
export CFD_MAX_STEPS=50000

# Case 1: m015i inviscid (block-Jacobi — no CFD_USE_LUSGS)
echo "[$(date +%H:%M:%S)] launching naca0012_m015_inviscid (block-Jacobi) np=$NP"
mkdir -p "$RESULTS_DIR/naca0012_m015_inviscid"
env -u CFD_USE_LUSGS CFD_PLATEAU_MIN_ORDERS=0.85 CFD_PLATEAU_FORCE_TOL=0.003 \
    "$MPI_BIN/mpirun" -np $NP "$ROOT/build/cfd_solver" solve \
    --case "$CASES_DIR/naca0012_m015_inviscid.json" \
    --output "$RESULTS_DIR/naca0012_m015_inviscid" \
    > "$RESULTS_DIR/naca0012_m015_inviscid/stdout.log" 2>&1 &
echo "  PID $!"

# Cases 2-8: LU-SGS
export CFD_USE_LUSGS=1
export CFD_LUSGS_RELAX=0.4
export CFD_LUSGS_DIAG_FACTOR=2.0
export CFD_PHYS_CFL_CAP=50
export CFD_PLATEAU_MIN_ORDERS=0.5

for cid in naca0012_m080_inviscid naca0012_m200_inviscid \
           naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 \
           naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20; do
    echo "[$(date +%H:%M:%S)] launching $cid (LU-SGS) np=$NP"
    mkdir -p "$RESULTS_DIR/$cid"
    "$MPI_BIN/mpirun" -np $NP "$ROOT/build/cfd_solver" solve \
        --case "$CASES_DIR/$cid.json" \
        --output "$RESULTS_DIR/$cid" \
        > "$RESULTS_DIR/$cid/stdout.log" 2>&1 &
    echo "  PID $!"
done

# Re200 transient (LU-SGS, no plateau check, just max steps)
echo "[$(date +%H:%M:%S)] launching cylinder_m010_laminar_re200 (LU-SGS transient) np=$NP"
mkdir -p "$RESULTS_DIR/cylinder_m010_laminar_re200"
"$MPI_BIN/mpirun" -np $NP "$ROOT/build/cfd_solver" solve \
    --case "$CASES_DIR/cylinder_m010_laminar_re200.json" \
    --output "$RESULTS_DIR/cylinder_m010_laminar_re200" \
    > "$RESULTS_DIR/cylinder_m010_laminar_re200/stdout.log" 2>&1 &
echo "  PID $!"

echo "All cases launched. PIDs: $(pgrep -f 'cfd_solver solve' | tr '\n' ' ')"
echo "Monitor: tail -f $RESULTS_DIR/<case>/stdout.log"

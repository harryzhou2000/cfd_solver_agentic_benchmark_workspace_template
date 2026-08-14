#!/bin/bash
# Run all 8 CFD benchmark cases
set -e

WORKSPACE="/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_m3_02"
SOLVER="$WORKSPACE/solver/build/cfd_solver"
CASES="$WORKSPACE/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="$WORKSPACE/solver/results"
RANKS=${1:-4}

export PATH="/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin:$PATH"
export LD_LIBRARY_PATH="/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/lib:$WORKSPACE/external/cfd_externals/install/lib:$LD_LIBRARY_PATH"

# Steady NACA inviscid cases
for case in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid; do
    echo "=== $case ==="
    mpirun --oversubscribe -np $RANKS $SOLVER solve \
        --case "$CASES/$case.json" --output "$RESULTS/$case" \
        --rusanov --cfl-cap 3 --sweeps 1 --first-order-steps 1000 \
        --max-limiter 0.15 --diag-safety 1.5 --max-steps 5000 2>&1 | tail -1
done

# Steady NACA laminar cases
for case in naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000; do
    echo "=== $case ==="
    mpirun --oversubscribe -np $RANKS $SOLVER solve \
        --case "$CASES/$case.json" --output "$RESULTS/$case" \
        --rusanov --cfl-cap 3 --sweeps 1 --first-order-steps 1000 \
        --max-limiter 0.15 --diag-safety 1.5 --max-steps 5000 2>&1 | tail -1
done

# Steady cylinder Re20
echo "=== cylinder_m010_laminar_re20 ==="
mpirun --oversubscribe -np $RANKS $SOLVER solve \
    --case "$CASES/cylinder_m010_laminar_re20.json" --output "$RESULTS/cylinder_m010_laminar_re20" \
    --rusanov --cfl-cap 3 --sweeps 1 --first-order --max-steps 5000 2>&1 | tail -1

# Transient cylinder Re200
echo "=== cylinder_m010_laminar_re200 ==="
mpirun --oversubscribe -np $RANKS $SOLVER solve \
    --case "$CASES/cylinder_m010_laminar_re200.json" --output "$RESULTS/cylinder_m010_laminar_re200" \
    --rusanov --cfl-cap 2 --max-limiter 0.0 --diag-safety 1.0 --max-inner 1 2>&1 | tail -1

# np=8 validation cases
echo "=== np=8 validation ==="
mpirun --oversubscribe -np 8 $SOLVER solve \
    --case "$CASES/naca0012_m015_inviscid.json" --output "$RESULTS/naca0012_m015_inviscid_np8" \
    --rusanov --cfl-cap 3 --sweeps 1 --first-order-steps 1000 \
    --max-limiter 0.15 --diag-safety 1.5 --max-steps 2000 2>&1 | tail -1
mpirun --oversubscribe -np 8 $SOLVER solve \
    --case "$CASES/cylinder_m010_laminar_re20.json" --output "$RESULTS/cylinder_m010_laminar_re20_np8" \
    --rusanov --cfl-cap 3 --sweeps 1 --first-order --max-steps 2000 2>&1 | tail -1

echo "=== All cases complete ==="

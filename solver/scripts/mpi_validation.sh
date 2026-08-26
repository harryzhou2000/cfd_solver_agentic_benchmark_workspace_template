#!/bin/bash
# MPI parallel validation: run NACA M=0.15 inviscid and cylinder Re20 with np=1,4,8
# np=2 results are the main run

SOLVER="/workspace/solver/build/cfd2d"
CASES_DIR="/workspace/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS="/workspace/solver/results"

# NACA0012 M=0.15 inviscid at different MPI ranks
for NP in 1 4 8; do
    echo "=== NACA0012 M=0.15 inviscid, np=$NP ==="
    OUTDIR="${RESULTS}/mpi_validation/naca0012_m015_inviscid_np${NP}"
    mkdir -p "$OUTDIR"
    mpirun --oversubscribe -np $NP $SOLVER solve \
        --case "${CASES_DIR}/naca0012_m015_inviscid.json" \
        --output "$OUTDIR" 2>&1 | tail -3
    echo ""
done

# Cylinder Re20 at different MPI ranks
for NP in 1 4 8; do
    echo "=== Cylinder Re20, np=$NP ==="
    OUTDIR="${RESULTS}/mpi_validation/cylinder_m010_laminar_re20_np${NP}"
    mkdir -p "$OUTDIR"
    mpirun --oversubscribe -np $NP $SOLVER solve \
        --case "${CASES_DIR}/cylinder_m010_laminar_re20.json" \
        --output "$OUTDIR" 2>&1 | tail -3
    echo ""
done

echo "=== MPI Validation Complete ==="
# Compare forces across rank counts
echo ""
echo "=== NACA0012 M=0.15 inviscid force comparison ==="
for NP in 1 2 4 8; do
    if [ "$NP" = "2" ]; then
        DIR="${RESULTS}/naca0012_m015_inviscid"
    else
        DIR="${RESULTS}/mpi_validation/naca0012_m015_inviscid_np${NP}"
    fi
    if [ -f "$DIR/forces.csv" ]; then
        python3 -c "
import csv
with open('$DIR/forces.csv') as f:
    rows = list(csv.DictReader(f))
last = rows[-1]
print(f'np={int(\"$NP\"):d}: Cd={float(last[\"cd\"]):12.8f}  Cl={float(last[\"cl\"]):12.8f}  steps={last[\"step\"]}')"
    fi
done

echo ""
echo "=== Cylinder Re20 force comparison ==="
for NP in 1 2 4 8; do
    if [ "$NP" = "2" ]; then
        DIR="${RESULTS}/cylinder_m010_laminar_re20"
    else
        DIR="${RESULTS}/mpi_validation/cylinder_m010_laminar_re20_np${NP}"
    fi
    if [ -f "$DIR/forces.csv" ]; then
        python3 -c "
import csv
with open('$DIR/forces.csv') as f:
    rows = list(csv.DictReader(f))
last = rows[-1]
print(f'np={int(\"$NP\"):d}: Cd={float(last[\"cd\"]):12.8f}  Cl={float(last[\"cl\"]):12.8f}  steps={last[\"step\"]}')"
    fi
done

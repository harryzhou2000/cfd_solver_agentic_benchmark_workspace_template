#!/bin/bash
set -e
SOLVER_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="$SOLVER_DIR/../cfd_solver_agentic_benchmark"
MPIRUN=/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin/mpirun
EXTROOT="$SOLVER_DIR/../external/cfd_externals/install"
export LD_LIBRARY_PATH="$EXTROOT/lib"
BIN="$SOLVER_DIR/build/cfd2d"
RESULTS="$SOLVER_DIR/results"
mkdir -p "$RESULTS"

run_case() {
  local case_file="$1" np="$2" extra="$3" outdir="$4" env_prefix="$5"
  echo "=== Running $(basename $outdir) (np=$np) ==="
  env $env_prefix LD_LIBRARY_PATH="$EXTROOT/lib" $MPIRUN --oversubscribe -np $np $BIN solve \
    --case "$case_file" --output "$outdir" $extra 2>&1 | tail -3
  echo "--- Done: $(basename $outdir) ---"
}

# Steady cases with np=1
for case_id in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
               naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
               cylinder_m010_laminar_re20; do
  run_case "$BENCH_DIR/inputs/cases/${case_id}.json" 1 "--max-steps 10000 --cfl-cap 2.0" "$RESULTS/${case_id}" ""
done

# Re200 transient
run_case "$BENCH_DIR/inputs/cases/cylinder_m010_laminar_re200.json" 1 "--cfl-cap 1.0" \
  "$RESULTS/cylinder_m010_laminar_re200" "CFD2D_FIRST_ORDER=1"

# MPI validation
for np in 2 4 8; do
  run_case "$BENCH_DIR/inputs/cases/naca0012_m015_inviscid.json" $np \
    "--max-steps 500 --cfl-cap 0.1" "$RESULTS/naca0012_m015_inviscid_np${np}" ""
  run_case "$BENCH_DIR/inputs/cases/cylinder_m010_laminar_re20.json" $np \
    "--max-steps 500 --cfl-cap 0.1" "$RESULTS/cylinder_m010_laminar_re20_np${np}" ""
done
echo "=== All cases complete ==="

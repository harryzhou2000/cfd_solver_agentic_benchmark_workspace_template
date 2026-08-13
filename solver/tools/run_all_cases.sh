#!/bin/bash
# Run all CFD benchmark cases
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOLVER_DIR="$(dirname "$SCRIPT_DIR")"
WORKSPACE="$(dirname "$SOLVER_DIR")"
BENCHMARK="$WORKSPACE/cfd_solver_agentic_benchmark"
CASES="$BENCHMARK/inputs/cases"
RESULTS="$SOLVER_DIR/results"
SOLVER_BIN="$SOLVER_DIR/build/cfd2d"

export PATH="$WORKSPACE/external/cfd_externals/install/bin:$PATH"
export LD_LIBRARY_PATH="$WORKSPACE/external/cfd_externals/install/lib:$LD_LIBRARY_PATH"

mkdir -p "$RESULTS"

NP=${NP:-4}
MAX_STEPS=${MAX_STEPS:-0}  # 0 = use case file default

run_case() {
    local case_file="$1"
    local case_name=$(basename "$case_file" .json)
    local np=${2:-$NP}
    local outdir="$RESULTS/$case_name"
    
    echo "============================================"
    echo "Running: $case_name (np=$np)"
    echo "============================================"
    
    mkdir -p "$outdir"
    
    local extra_env=""
    if [ "$MAX_STEPS" -gt 0 ]; then
        extra_env="CFD2D_MAX_STEPS=$MAX_STEPS"
    fi
    
    env $extra_env mpirun --allow-run-as-root -np $np "$SOLVER_BIN" solve \
        --case "$case_file" \
        --output "$outdir" 2>&1 | tee "$outdir/run_log.txt"
    
    echo "Done: $case_name"
}

# Run all steady cases
for case in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
            naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
            cylinder_m010_laminar_re20; do
    run_case "$CASES/$case.json"
done

# Run Re 200 transient case
run_case "$CASES/cylinder_m010_laminar_re200.json"

echo "All cases completed."

#!/bin/bash
# CFD Solver Benchmark — Production Case Runner
# Run all 8 required cases with production parameters.
# Usage: ./run_cases.sh [NP_RANKS] [CASE_JSON_DIR]
#   NP_RANKS: number of MPI ranks (default: 4)
#   CASE_JSON_DIR: path to case JSON files (default: ../cfd_solver_agentic_benchmark/inputs/cases)
#
# Results go to solver/results/<case_id>/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SOLVER="${BUILD_DIR}/cfd_solver"
RESULTS_DIR="${SCRIPT_DIR}/results"
CASE_DIR="${2:-${SCRIPT_DIR}/../cfd_solver_agentic_benchmark/inputs/cases}"
NP="${1:-4}"

# External library path
EXTLIB="$(realpath "${SCRIPT_DIR}/../external/cfd_externals/install/lib")"
export LD_LIBRARY_PATH="${EXTLIB}:${LD_LIBRARY_PATH:-}"

mkdir -p "${RESULTS_DIR}"

# Case list with estimated run times
# Format: case_file | type | estimated_steps | notes
declare -a CASES=(
    "naca0012_m015_inviscid.json"
    "naca0012_m080_inviscid.json"
    "naca0012_m200_inviscid.json"
    "naca0012_m015_laminar_re5000.json"
    "naca0012_m080_laminar_re5000.json"
    "naca0012_m200_laminar_re5000.json"
    "cylinder_m010_laminar_re20.json"
    "cylinder_m010_laminar_re200.json"
)

echo "=== CFD Solver Benchmark — Production Run ==="
echo "Solver: ${SOLVER}"
echo "MPI ranks: ${NP}"
echo "Results dir: ${RESULTS_DIR}"
echo ""

for case_file in "${CASES[@]}"; do
    case_id="${case_file%.json}"
    case_path="${CASE_DIR}/${case_file}"
    output_dir="${RESULTS_DIR}/${case_id}"
    
    echo "--- Running: ${case_id} ---"
    echo "  Case: ${case_path}"
    echo "  Output: ${output_dir}"
    echo "  Started: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    
    mkdir -p "${output_dir}"
    
    start_time=$(date +%s)
    
    if mpirun -np "${NP}" "${SOLVER}" solve \
        --case "${case_path}" \
        --output "${output_dir}" \
        2>&1 | tee "${output_dir}/terminal.log"; then
        end_time=$(date +%s)
        elapsed=$((end_time - start_time))
        hours=$((elapsed / 3600))
        mins=$(((elapsed % 3600) / 60))
        secs=$((elapsed % 60))
        echo "  Status: COMPLETED (exit 0)"
        echo "  Wall time: ${hours}h ${mins}m ${secs}s"
    else
        end_time=$(date +%s)
        elapsed=$((end_time - start_time))
        echo "  Status: FAILED (exit non-zero)"
        echo "  Wall time: ${elapsed}s"
    fi
    echo ""
done

echo "=== All cases finished ==="
echo "Results in: ${RESULTS_DIR}"

# Print summary
echo ""
echo "=== Result Summary ==="
for case_file in "${CASES[@]}"; do
    case_id="${case_file%.json}"
    status_file="${RESULTS_DIR}/${case_id}/run_status.json"
    if [ -f "${status_file}" ]; then
        status=$(python3 -c "import json; d=json.load(open('${status_file}')); print(d.get('convergence_status','?'))" 2>/dev/null || echo "parse_error")
        time=$(python3 -c "import json; d=json.load(open('${status_file}')); print(f\"{d.get('wall_time_seconds',0):.1f}s\")" 2>/dev/null || echo "?")
        echo "  ${case_id}: ${status} (${time})"
    else
        echo "  ${case_id}: no run_status.json"
    fi
done

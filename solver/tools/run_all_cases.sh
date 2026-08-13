#!/usr/bin/env bash
# =============================================================================
# run_all_cases.sh
#
# Runs all 8 required benchmark cases for the 2-D unstructured compressible
# Navier-Stokes solver, plus two np=8 MPI scaling runs.
#
# Usage:
#   bash solver/tools/run_all_cases.sh [build|nobuild]
#
#   build   (default) - rebuild if the binary is stale or missing
#   nobuild            - skip the build step entirely
#
# The script assumes it is run from the repository root.
# =============================================================================
set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

export LD_LIBRARY_PATH="external/cfd_externals/install/lib:${LD_LIBRARY_PATH:-}"

SOLVER_BIN="solver/build/cfd2d"
CASES_DIR="cfd_solver_agentic_benchmark/inputs/cases"
RESULTS_DIR="solver/results"
LOG_DIR="${RESULTS_DIR}/run_logs"
MANIFEST="${RESULTS_DIR}/run_manifest.csv"

PYTHON="${REPO_ROOT}/.venv/bin/python"

mkdir -p "$RESULTS_DIR" "$LOG_DIR"

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

BUILD_MODE="${1:-build}"

build_solver() {
    if [[ "$BUILD_MODE" == "nobuild" ]]; then
        if [[ -x "$SOLVER_BIN" ]]; then
            echo "[run_all] Skipping build (nobuild); using existing $SOLVER_BIN"
            return
        else
            echo "[run_all] Binary missing but --nobuild requested; building anyway."
        fi
    fi

    if [[ -x "$SOLVER_BIN" ]] && [[ "$BUILD_MODE" != "build" ]]; then
        echo "[run_all] Binary exists and build not explicitly requested; skipping."
        return
    fi

    echo "[run_all] Building solver..."
    mkdir -p solver/build
    cd solver/build
    cmake .. \
        -DCFD_EXTERNALS_ROOT="$REPO_ROOT/external/cfd_externals/install" \
        -DEXTERNAL_HEADER_ROOT="$REPO_ROOT/external" \
        -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc)"
    cd "$REPO_ROOT"
    echo "[run_all] Build complete: $SOLVER_BIN"
}

build_solver

# ---------------------------------------------------------------------------
# Helper: run one case and record timing/status
# ---------------------------------------------------------------------------

if [[ ! -f "$MANIFEST" ]]; then
    echo "case_id,command,mpi_ranks,wall_time,convergence_status,final_cd,final_cl" > "$MANIFEST"
fi

run_case() {
    local case_id="$1"
    local case_json="$2"
    local np="$3"
    local out_dir="${RESULTS_DIR}/${case_id}"
    local log_file="${LOG_DIR}/${case_id}.log"

    echo "============================================================"
    echo "[run_all] Case: $case_id  (np=$np)"
    echo "[run_all]   case:   $case_json"
    echo "[run_all]   output: $out_dir"
    echo "============================================================"

    mkdir -p "$out_dir"

    local cmd="mpirun -np ${np} ${SOLVER_BIN} solve --case ${case_json} --output ${out_dir} --report-level full"
    echo "[run_all] Command: $cmd"

    local start_ts end_ts wall_time exit_code
    start_ts=$(date +%s.%N)

    set +e
    $cmd 2>&1 | tee "$log_file"
    exit_code=${PIPESTATUS[0]}
    set -e

    end_ts=$(date +%s.%N)
    wall_time=$(echo "$end_ts - $start_ts" | bc)

    local conv_status="unknown"
    local final_cd=""
    local final_cl=""

    if [[ -f "${out_dir}/run_status.json" ]]; then
        conv_status=$("$PYTHON" -c "
import json
try:
    with open('${out_dir}/run_status.json') as f:
        d = json.load(f)
    print(d.get('convergence_status', 'unknown'))
except Exception:
    print('unknown')
")
    fi

    if [[ -f "${out_dir}/forces.csv" ]]; then
        final_cd=$("$PYTHON" -c "
import csv
try:
    with open('${out_dir}/forces.csv') as f:
        rows = list(csv.DictReader(f))
    if rows:
        print(f'{float(rows[-1][\"cd\"]):.6e}')
except Exception:
    pass
")
        final_cl=$("$PYTHON" -c "
import csv
try:
    with open('${out_dir}/forces.csv') as f:
        rows = list(csv.DictReader(f))
    if rows:
        print(f'{float(rows[-1][\"cl\"]):.6e}')
except Exception:
    pass
")
    fi

    echo "[run_all] $case_id: status=$conv_status, wall=${wall_time}s, CD=${final_cd:-N/A}, CL=${final_cl:-N/A}"

    local escaped_cmd
    escaped_cmd=$(echo "$cmd" | sed 's/,/;/g')
    echo "${case_id},${escaped_cmd},${np},${wall_time},${conv_status},${final_cd:-},${final_cl:-}" >> "$MANIFEST"

    if [[ $exit_code -ne 0 ]]; then
        echo "[run_all] WARNING: $case_id exited with code $exit_code"
    fi
}

# ---------------------------------------------------------------------------
# Run the 8 required cases
# ---------------------------------------------------------------------------

# NACA0012 cases — np=4
run_case "naca0012_m015_inviscid"       "${CASES_DIR}/naca0012_m015_inviscid.json"       4
run_case "naca0012_m080_inviscid"       "${CASES_DIR}/naca0012_m080_inviscid.json"       4
run_case "naca0012_m200_inviscid"       "${CASES_DIR}/naca0012_m200_inviscid.json"       4
run_case "naca0012_m015_laminar_re5000" "${CASES_DIR}/naca0012_m015_laminar_re5000.json" 4
run_case "naca0012_m080_laminar_re5000" "${CASES_DIR}/naca0012_m080_laminar_re5000.json" 4
run_case "naca0012_m200_laminar_re5000" "${CASES_DIR}/naca0012_m200_laminar_re5000.json" 4

# Cylinder cases — np=4 for Re20, np=8 for Re200
run_case "cylinder_m010_laminar_re20"   "${CASES_DIR}/cylinder_m010_laminar_re20.json"   4
run_case "cylinder_m010_laminar_re200"  "${CASES_DIR}/cylinder_m010_laminar_re200.json"  8

# ---------------------------------------------------------------------------
# MPI rank-count comparison: np=8 for one NACA and one cylinder case
# ---------------------------------------------------------------------------
run_case "naca0012_m015_inviscid_np8"     "${CASES_DIR}/naca0012_m015_inviscid.json"     8
run_case "cylinder_m010_laminar_re20_np8" "${CASES_DIR}/cylinder_m010_laminar_re20.json" 8

# ---------------------------------------------------------------------------
# Generate report data and figures
# ---------------------------------------------------------------------------
echo "============================================================"
echo "[run_all] Generating report tables, sanity checks, and figures..."
echo "============================================================"
"$PYTHON" solver/tools/generate_report.py

echo "[run_all] All cases complete."
echo "[run_all] Manifest: $MANIFEST"
echo "[run_all] Results:  $RESULTS_DIR/"
echo "[run_all] Report:   solver/report/report.tex"
echo "[run_all] Logs:     $LOG_DIR/"

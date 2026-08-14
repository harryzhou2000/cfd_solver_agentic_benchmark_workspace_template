#!/usr/bin/env bash
# Production case launcher for the cfd_agentic_benchmark.
#
# Usage:
#   ./tools/run_production.sh <case_id> [<case_id> ...]
#   ./tools/run_production.sh all
#
# Runs each case at np=8 with the production convergence configuration:
#   CFD_LUSGS_RELAX=0.4        under-relaxed scalar LU-SGS update
#   CFD_LUSGS_DIAG_FACTOR=2.0  strengthened diagonal (full spectral radius)
#   plateau detection          accept a credibly plateaued steady state
#
# Outputs go to solver/results/<case_id>/ with stdout.log captured.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MPI_BIN="${MPI_BIN:-/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin}"
CASES_DIR="$ROOT/cfd_solver_agentic_benchmark/inputs/cases"
RESULTS_DIR="$ROOT/solver/results"
NP="${NP:-8}"

export CFD_LUSGS_RELAX="${CFD_LUSGS_RELAX:-0.4}"
export CFD_LUSGS_DIAG_FACTOR="${CFD_LUSGS_DIAG_FACTOR:-2.0}"
export CFD_PLATEAU_WINDOW="${CFD_PLATEAU_WINDOW:-300}"
export CFD_PLATEAU_RES_TOL="${CFD_PLATEAU_RES_TOL:-0.15}"
export CFD_PLATEAU_FORCE_TOL="${CFD_PLATEAU_FORCE_TOL:-0.05}"
export CFD_PLATEAU_MIN_ORDERS="${CFD_PLATEAU_MIN_ORDERS:-1.0}"

ALL_CASES=(naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid
           naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000
           naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20
           cylinder_m010_laminar_re200)

if [ $# -eq 0 ]; then
    echo "usage: $0 <case_id> [...] | all" >&2
    exit 2
fi

if [ "$1" = "all" ]; then
    CASES=("${ALL_CASES[@]}")
else
    CASES=("$@")
fi

mkdir -p "$RESULTS_DIR"
for cid in "${CASES[@]}"; do
    case_json="$CASES_DIR/$cid.json"
    out_dir="$RESULTS_DIR/$cid"
    if [ ! -f "$case_json" ]; then
        echo "ERROR: missing case file $case_json" >&2
        exit 1
    fi
    rm -rf "$out_dir"
    mkdir -p "$out_dir"
    echo "[$(date +%H:%M:%S)] launching $cid np=$NP -> $out_dir"
    (
        cd "$ROOT" || exit 1
        PATH="$MPI_BIN:$PATH" "$MPI_BIN/mpirun" -np "$NP" \
            solver/build/cfd_solver solve \
            --case "cfd_solver_agentic_benchmark/inputs/cases/$cid.json" \
            --output "solver/results/$cid" \
            --report-level full \
            > "solver/results/$cid/stdout.log" 2>&1
        echo "[$(date +%H:%M:%S)] finished $cid rc=$?" >> "solver/results/$cid/stdout.log"
    ) &
done

echo "launched ${#CASES[@]} case(s); monitor with: tail -f $RESULTS_DIR/<case>/stdout.log"

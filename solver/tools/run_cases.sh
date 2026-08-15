#!/bin/bash
# Full reproduction: build, run all cases, consistency checks, plots, report.
# Usage: tools/run_cases.sh <benchmark_root>
set -euo pipefail

BENCH=${1:-/workspace/cfd_solver_agentic_benchmark}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOLVER=$ROOT/build/fv2d
CASES=$BENCH/inputs/cases

# --- build ---
mkdir -p "$ROOT/build"
cd "$ROOT/build"
cmake .. -DCFD_EXTERNALS_ROOT="${CFD_EXTERNALS_ROOT:-/workspace/external/cfd_externals/install}" \
         -DCFD_HEADER_ONLY_ROOT="${CFD_HEADER_ONLY_ROOT:-/workspace/external}"
make -j8
cd "$ROOT"

# --- production runs ---
run() { # ranks case out [extra...]
  local np=$1 case=$2 out=$3; shift 3
  mpirun -np "$np" "$SOLVER" solve --case "$CASES/$case.json" --output "$ROOT/results/$out" "$@"
}

run 8  naca0012_m015_inviscid        naca0012_m015_inviscid        --max-steps 60000 &
run 8  naca0012_m080_inviscid        naca0012_m080_inviscid        &
run 8  naca0012_m200_inviscid        naca0012_m200_inviscid        &
run 8  naca0012_m015_laminar_re5000  naca0012_m015_laminar_re5000  &
wait
run 8  naca0012_m080_laminar_re5000  naca0012_m080_laminar_re5000  &
run 8  naca0012_m200_laminar_re5000  naca0012_m200_laminar_re5000  &
run 8  cylinder_m010_laminar_re20    cylinder_m010_laminar_re20    &
wait
run 16 cylinder_m010_laminar_re200   cylinder_m010_laminar_re200   --inner-cfl 10 --inner-sweeps 2

# --- rank-count consistency checks ---
mkdir -p "$ROOT/results_consistency"
for np in 1 2 4; do
  mpirun -np $np "$SOLVER" solve --case "$CASES/naca0012_m015_inviscid.json" \
    --max-steps 60000 --output "$ROOT/results_consistency/naca0012_m015_inviscid_np$np"
  mpirun -np $np "$SOLVER" solve --case "$CASES/cylinder_m010_laminar_re20.json" \
    --output "$ROOT/results_consistency/cylinder_m010_laminar_re20_np$np"
done

# --- post-processing + report assets ---
.venv/bin/python tools/plot_all.py "$ROOT/results" "$ROOT/report"
.venv/bin/python tools/make_manifest.py "$ROOT/results" "$ROOT/results_consistency" "$ROOT/report"
.venv/bin/python tools/sanity_checks.py "$ROOT/results" "$ROOT/report"
python3 "$BENCH/examiner/validate_outputs.py" "$ROOT"/results/* --report "$ROOT/report"
cd "$ROOT/report" && pdflatex -interaction=nonstopmode report.tex

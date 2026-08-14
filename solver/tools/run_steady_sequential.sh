#!/usr/bin/env bash
# Sequential np=1 production runner for the seven steady benchmark cases.
# Run this alongside the np=3 Re=200 transient job: the transient job uses
# three of the container's four CPU cores and this script uses the fourth.
set -euo pipefail

cd "$(dirname "$0")/.."
export OMPI_ALLOW_RUN_AS_ROOT=1
export OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

run_case() {
    local case_id="$1"
    local relax="$2"
    local freeze="$3"
    local extra="${4:-}"
    local case_json="../cfd_solver_agentic_benchmark/inputs/cases/${case_id}.json"
    local out="results/final_${case_id}_np1"
    echo "[run] $case_id relax=$relax freeze=$freeze out=$out"
    if [ -n "$extra" ]; then
        env CFD_RELAX="$relax" CFD_FREEZE_RECON_AFTER="$freeze" \
            "$extra" ./build/cfd_fv2d solve \
            --case "$case_json" --output "$out"
    else
        env CFD_RELAX="$relax" CFD_FREEZE_RECON_AFTER="$freeze" \
            ./build/cfd_fv2d solve \
            --case "$case_json" --output "$out"
    fi
}

run_case cylinder_m010_laminar_re20 1.2 500 CFD_RUSANOV=1
run_case naca0012_m080_laminar_re5000 1.0 800
run_case naca0012_m200_laminar_re5000 1.0 800
run_case naca0012_m015_laminar_re5000 1.0 800
run_case naca0012_m080_inviscid 1.0 800
run_case naca0012_m200_inviscid 1.0 800
run_case naca0012_m015_inviscid 1.0 800

echo "[run] steady cases finished"

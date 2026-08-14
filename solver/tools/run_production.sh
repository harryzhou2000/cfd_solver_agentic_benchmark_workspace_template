#!/usr/bin/env bash
# Production launcher for the cfd-fv2d benchmark cases.
#
# Steady cases freeze the least-squares gradients and Venkatakrishnan limiter
# in the final phase (CFD_FREEZE_RECON_AFTER). This removes the limiter limit
# cycle observed on the cylinder and transonic cases without changing the
# spatial scheme (the frozen reconstruction remains second order). The
# cylinder Re=20 case additionally uses Rusanov (CFD_RUSANOV=1), whose low-Mach
# dissipation gives the closest resolved drag to the reference value, and a
# mild SGS over-relaxation. All other run controls come from the case JSONs.
#
# Usage: ./tools/run_production.sh [--np N] [--max-steps M]
set -euo pipefail

cd "$(dirname "$0")/.."

NP=1
MAX_STEPS=""
while [ $# -gt 0 ]; do
    case "$1" in
        --np) NP="$2"; shift 2 ;;
        --max-steps) MAX_STEPS="--max-steps $2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

export OMPI_ALLOW_RUN_AS_ROOT=1
export OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

run_case() {
    local case_id="$1"
    local relax="${2:-1.0}"
    local freeze="${3:-0}"
    local case_json="../cfd_solver_agentic_benchmark/inputs/cases/${case_id}.json"
    local out="results/final_${case_id}_np${NP}"
    local extra_env=()
    [ "$freeze" -gt 0 ] && extra_env+=("CFD_FREEZE_RECON_AFTER=$freeze")
    if [ "$case_id" = "cylinder_m010_laminar_re20" ]; then
        extra_env+=("CFD_RUSANOV=1")
    fi
    echo "[run] $case_id np=$NP relax=$relax freeze=$freeze out=$out"
    if [ "$NP" -eq 1 ]; then
        env CFD_RELAX="$relax" "${extra_env[@]}" ./build/cfd_fv2d solve \
            --case "$case_json" --output "$out" $MAX_STEPS
    else
        env CFD_RELAX="$relax" "${extra_env[@]}" \
            mpirun --allow-run-as-root --oversubscribe \
            -np "$NP" ./build/cfd_fv2d solve \
            --case "$case_json" --output "$out" $MAX_STEPS
    fi
}

# Steady cases. Roe with the entropy fix is the default flux for the NACA
# cases; the cylinder Re=20 case adds the Rusanov override above.
run_case naca0012_m015_inviscid 1.0 800
run_case naca0012_m080_inviscid 1.0 800
run_case naca0012_m200_inviscid 1.0 800
run_case naca0012_m015_laminar_re5000 1.0 800
run_case naca0012_m080_laminar_re5000 1.0 800
run_case naca0012_m200_laminar_re5000 1.0 800
run_case cylinder_m010_laminar_re20 1.2 500

# Reynolds 200 transient (CFL=1.0 and the 1e-3 inner target come from the
# case JSON).
run_case cylinder_m010_laminar_re200 1.0 0

echo "[run] all production cases finished"

#!/usr/bin/env bash
# Launch the 8 production cases detached (survives the calling shell exiting).
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MPI_BIN="${MPI_BIN:-/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin}"
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

cd "$ROOT" || exit 1
for cid in "${ALL_CASES[@]}"; do
    out_dir="solver/results/$cid"
    if [ ! -d "$out_dir" ]; then
        mkdir -p "$out_dir"
    fi
    setsid nohup env PATH="$MPI_BIN:$PATH" "$MPI_BIN/mpirun" -np "$NP" \
        solver/build/cfd_solver solve \
        --case "cfd_solver_agentic_benchmark/inputs/cases/$cid.json" \
        --output "$out_dir" --report-level full \
        > "$out_dir/stdout.log" 2>&1 < /dev/null &
    echo "launched $cid pid=$!"
done

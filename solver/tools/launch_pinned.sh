#!/usr/bin/env bash
# Launch one solver case pinned to a contiguous core range.
#
# Usage:
#   ./tools/launch_pinned.sh <np> <first_cpu> <case_json> <out_dir> [solver args...]
#
# Without explicit pinning, several concurrent mpirun jobs all bind to the
# same first cores of the machine and thrash.  This launcher runs the job,
# waits for the solver ranks to appear, and pins rank i to core
# first_cpu+i.  Environment variables (CFD_USE_LUSGS, CFD_LUSGS_RELAX, ...)
# are inherited by the mpirun ranks.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MPI_BIN="${MPI_BIN:-/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install/bin}"

if [ $# -lt 4 ]; then
    echo "usage: $0 <np> <first_cpu> <case_json> <out_dir> [solver args...]" >&2
    exit 64
fi
np="$1"; first_cpu="$2"; case_json="$3"; out_dir="$4"; shift 4

if ! [[ "$np" =~ ^[1-9][0-9]*$ && "$first_cpu" =~ ^[0-9]+$ ]]; then
    echo "np and first_cpu must be positive integers" >&2
    exit 64
fi
if ! [[ -f "$case_json" ]]; then
    echo "missing case JSON: $case_json" >&2
    exit 66
fi
if ! [[ -d "$out_dir" ]]; then
    mkdir -p "$out_dir"
fi

cd "$ROOT" || exit 1
PATH="$MPI_BIN:$PATH" "$MPI_BIN/mpirun" -np "$np" \
    ./solver/build/cfd_solver solve \
    --case "$case_json" --output "$out_dir" --report-level full "$@" \
    > "$out_dir/stdout.log" 2>&1 &
mpirun_pid=$!

# Pin each solver rank to its own core once the ranks are up.
rank_pids=()
for _attempt in $(seq 1 300); do
    rank_pids=($(ps -ww -eo pid=,comm=,args= | awk -v out="$out_dir" \
        '$2 == "cfd_solver" && index($0, "--output " out " ") {print $1}'))
    if [ "${#rank_pids[@]}" -ge "$np" ]; then
        break
    fi
    sleep 0.1
done
cpu="$first_cpu"
for rank_pid in "${rank_pids[@]}"; do
    if [ "$cpu" -lt "$((first_cpu + np))" ]; then
        taskset -pc "$cpu" "$rank_pid" >/dev/null 2>&1
    fi
    cpu=$((cpu + 1))
done
echo "launched $out_dir (np=$np, cpus=${first_cpu}-$((first_cpu + np - 1)), mpirun_pid=$mpirun_pid)"

wait "$mpirun_pid"
echo "finished $out_dir rc=$?"

#!/usr/bin/env bash
set -euo pipefail

if (( $# < 4 )); then
    echo "usage: $0 <mpi-ranks> <first-cpu> <case-json> <output-dir> [solver-options ...]" >&2
    exit 64
fi

mpi_ranks=$1
first_cpu=$2
case_json=$3
output_dir=$4
shift 4

if ! [[ $mpi_ranks =~ ^[1-9][0-9]*$ && $first_cpu =~ ^[0-9]+$ ]]; then
    echo "mpi-ranks must be positive and first-cpu must be non-negative" >&2
    exit 64
fi
if [[ ! -f $case_json ]]; then
    echo "missing case JSON: $case_json" >&2
    exit 66
fi
if [[ -e $output_dir && ! -d $output_dir ]]; then
    echo "output path exists and is not a directory: $output_dir" >&2
    exit 73
fi
if [[ -d $output_dir ]] && find "$output_dir" -mindepth 1 -print -quit | grep -q .; then
    echo "refusing non-empty output directory: $output_dir" >&2
    exit 73
fi

available_cpus=$(nproc)
last_cpu=$((first_cpu + mpi_ranks - 1))
if (( last_cpu >= available_cpus )); then
    echo "requested CPU range ${first_cpu}-${last_cpu} exceeds available CPUs" >&2
    exit 64
fi

mkdir -p "$(dirname "$output_dir")"
launch_log="${output_dir}.launch.log"
pid_file="${output_dir}.launcher.pid"
status_file="${output_dir}.launcher.status"
if [[ -e $launch_log || -e $pid_file || -e $status_file ]]; then
    echo "refusing to overwrite existing launcher evidence for $output_dir" >&2
    exit 73
fi

exec >>"$launch_log" 2>&1
printf 'launcher_pid=%d\n' "$$"
printf 'working_directory=%s\n' "$PWD"
printf 'command='
printf '%q ' mpirun -np "$mpi_ranks" solver/build/cfd_solver solve \
    --case "$case_json" --output "$output_dir" --report-level full "$@"
printf '\n'
printf '%d\n' "$$" >"$pid_file"

mpirun -np "$mpi_ranks" solver/build/cfd_solver solve \
    --case "$case_json" --output "$output_dir" --report-level full "$@" &
mpirun_pid=$!
printf 'mpirun_pid=%d\n' "$mpirun_pid"

rank_pids=()
for _attempt in $(seq 1 200); do
    mapfile -t rank_pids < <(
        ps -eo pid=,comm=,args= | awk -v marker="--output $output_dir " \
            '$2 == "cfd_solver" && index($0, marker) {print $1}'
    )
    if (( ${#rank_pids[@]} == mpi_ranks )); then
        break
    fi
    sleep 0.1
done
if (( ${#rank_pids[@]} != mpi_ranks )); then
    echo "could not resolve exactly $mpi_ranks solver ranks; found ${#rank_pids[@]}" >&2
    kill "$mpirun_pid" 2>/dev/null || true
    wait "$mpirun_pid" 2>/dev/null || true
    printf 'launcher_exit_status=1\n' >"$status_file"
    exit 1
fi

cpu=$first_cpu
for rank_pid in "${rank_pids[@]}"; do
    taskset -pc "$cpu" "$rank_pid"
    printf 'rank_pid=%d cpu=%d\n' "$rank_pid" "$cpu"
    cpu=$((cpu + 1))
done

set +e
wait "$mpirun_pid"
solver_status=$?
set -e
printf 'launcher_exit_status=%d\n' "$solver_status" >"$status_file"
printf 'launcher_exit_status=%d\n' "$solver_status"
exit "$solver_status"

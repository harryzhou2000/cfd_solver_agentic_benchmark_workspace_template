#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
supervisor="$repo_root/solver/tools/supervise_transient_case.sh"
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

case_json="$temporary/case.json"
cat >"$case_json" <<'JSON'
{"case_id":"fixture_re200","run_control":{"type":"transient","time_step":0.01,"final_time":300.0}}
JSON

fake_launcher="$temporary/fake-launcher.sh"
cat >"$fake_launcher" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
ranks=$1; cpu=$2; case_json=$3; output=$4
shift 4
printf '%q ' "$@" >>"$CALL_LOG"
printf '\n' >>"$CALL_LOG"
count=$(cat "$COUNT_FILE" 2>/dev/null || printf 0)
count=$((count + 1))
printf '%s\n' "$count" >"$COUNT_FILE"

write_unfinished() {
    mkdir -p "$output"
    for file in stdout.log residuals.csv forces.csv surface.csv partition_diagnostics.csv; do : >"$output/$file"; done
    printf 'CFDTRN01' >"$output/transient_checkpoint.bin"
    cat >"$output/transient_checkpoint.json" <<JSON
{"format":"CFDTRN01","version":1,"case_id":"fixture_re200","accepted_step":100,"physical_time":1.0,"time_step":0.01,"force_history_samples":100,"inner_iteration_samples":100}
JSON
}
write_completed() {
    cat >"$output/run_status.json" <<JSON
{"case_id":"fixture_re200","command":"fixture solve","notes":"fixture complete","mpi_ranks":2,"final_step":30000,"final_physical_time":300.0,"convergence_status":"statistically_periodic"}
JSON
    cat >"$output/metadata.json" <<JSON
{"case_id":"fixture_re200","completed":true,"convergence_status":"statistically_periodic"}
JSON
}
write_failed() {
    mkdir -p "$output"
    cat >"$output/run_status.json" <<JSON
{"case_id":"fixture_re200","command":"fixture solve","notes":"fixture failure","mpi_ranks":2,"final_step":100,"final_physical_time":1.0,"convergence_status":"failed"}
JSON
}
case "${FAKE_MODE:?}" in
    interrupt_then_complete)
        if [[ ! -e $output ]]; then write_unfinished; exit 75; fi
        write_completed
        ;;
    failed) write_failed ;;
    mismatch_checkpoint)
        write_unfinished
        sed -i 's/fixture_re200/not_the_case/' "$output/transient_checkpoint.json"
        exit 75
        ;;
    hold)
        sleep 2
        write_failed
        ;;
    *) exit 99 ;;
esac
SH
chmod +x "$fake_launcher"

fake_solver="$temporary/fake-solver"
printf '#!/usr/bin/env bash\nexit 0\n' >"$fake_solver"
chmod +x "$fake_solver"

run_supervisor() {
    CFD_TRANSIENT_LAUNCHER="$fake_launcher" CFD_TRANSIENT_SOLVER_BINARY="$fake_solver" \
        CALL_LOG="$temporary/calls" COUNT_FILE="$temporary/count" \
        FAKE_MODE="$1" "$supervisor" 2 0 "$case_json" "$2" --restart "$temporary/precursor.bin" --restart-perturbation true
}

expect_status() {
    local expected=$1
    shift
    local output_file="$temporary/command.$RANDOM.out"
    set +e
    "$@" >"$output_file" 2>&1
    local actual=$?
    set -e
    [[ $actual == "$expected" ]] || {
        echo "expected status $expected, got $actual" >&2
        sed -n '1,120p' "$output_file" >&2
        exit 1
    }
}

# First invocation is the only initial launch; systemd's next invocation uses
# the same package and selects --resume.  Each call is observed by the fake.
output="$temporary/complete"
expect_status 75 run_supervisor interrupt_then_complete "$output"
expect_status 0 run_supervisor interrupt_then_complete "$output"
[[ $(cat "$temporary/count") == 2 ]]
[[ $(sed -n '1p' "$temporary/calls") == *'--restart'* ]]
[[ $(sed -n '1p' "$temporary/calls") != *'--resume'* ]]
[[ $(sed -n '2p' "$temporary/calls") == *'--resume'* ]]
[[ $(sed -n '2p' "$temporary/calls") != *'--restart'* ]]

# The exact solver binary is immutable across continuation segments.  A
# changed executable is refused before another launcher can run.
binary_output="$temporary/binary-mismatch"
expect_status 75 run_supervisor interrupt_then_complete "$binary_output"
printf '# changed\n' >>"$fake_solver"
expect_status 69 run_supervisor interrupt_then_complete "$binary_output"
[[ $(cat "$temporary/count") == 3 ]]

# An explicit failed solver status is terminal, so Restart=on-failure does not
# endlessly rerun a numerically failed case.
failed_output="$temporary/failed"
expect_status 0 run_supervisor failed "$failed_output"
[[ $(cat "$temporary/count") == 4 ]]

# A managed but mismatched checkpoint is refused before a second launcher call.
bad_output="$temporary/mismatch"
expect_status 69 run_supervisor mismatch_checkpoint "$bad_output"
[[ $(cat "$temporary/count") == 5 ]]

# A pre-existing package without supervisor state is never assumed to be safe.
unmanaged="$temporary/unmanaged"
mkdir "$unmanaged"
expect_status 69 run_supervisor interrupt_then_complete "$unmanaged"
[[ $(cat "$temporary/count") == 5 ]]

# The advisory flock stops a concurrent service before it can launch another
# MPI job for the same output package.
locked="$temporary/locked"
CFD_TRANSIENT_LAUNCHER="$fake_launcher" CFD_TRANSIENT_SOLVER_BINARY="$fake_solver" \
    CALL_LOG="$temporary/calls" COUNT_FILE="$temporary/count" \
    FAKE_MODE=hold "$supervisor" 2 0 "$case_json" "$locked" >"$temporary/holder.out" 2>&1 &
holder=$!
for _ in $(seq 1 40); do [[ -e "${locked}.transient-supervisor.lock" ]] && break; sleep 0.05; done
expect_status 73 run_supervisor hold "$locked"
wait "$holder"
[[ $(cat "$temporary/count") == 6 ]]

echo "supervise_transient_case.sh tests passed"

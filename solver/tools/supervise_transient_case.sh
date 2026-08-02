#!/usr/bin/env bash
# Restart-safe wrapper for one transient production package.
#
# It is intentionally suitable for `systemd-run --property=Restart=on-failure`:
# a non-terminal launcher exit is returned to systemd, and the next invocation
# resumes from the durable checkpoint instead of repeating the initial launch.
set -euo pipefail

readonly EX_USAGE=64
readonly EX_NOINPUT=66
readonly EX_UNAVAILABLE=69
readonly EX_CANTCREAT=73
readonly EX_TEMPFAIL=75

usage() {
    echo "usage: $0 <mpi-ranks> <first-cpu> <transient-case-json> <output-dir> [initial solver options ...]" >&2
}

if (( $# < 4 )); then
    usage
    exit "$EX_USAGE"
fi

mpi_ranks=$1
first_cpu=$2
case_json=$3
output_dir=$4
shift 4
initial_options=("$@")

if ! [[ $mpi_ranks =~ ^[1-9][0-9]*$ && $first_cpu =~ ^[0-9]+$ ]]; then
    echo "mpi-ranks must be positive and first-cpu must be non-negative" >&2
    exit "$EX_USAGE"
fi
if [[ ! -f $case_json ]]; then
    echo "missing case JSON: $case_json" >&2
    exit "$EX_NOINPUT"
fi

# realpath -m allows the as-yet-uncreated initial output directory.
case_json=$(realpath "$case_json")
output_dir=$(realpath -m "$output_dir")
state_file="${output_dir}.transient-supervisor.state"
lock_file="${output_dir}.transient-supervisor.lock"
event_file="${output_dir}.transient-supervisor.events"
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
launcher=${CFD_TRANSIENT_LAUNCHER:-"$script_dir/launch_pinned_case.sh"}
solver_binary=${CFD_TRANSIENT_SOLVER_BINARY:-solver/build/cfd_solver}

if [[ ! -x $launcher ]]; then
    echo "missing executable launcher: $launcher" >&2
    exit "$EX_NOINPUT"
fi
if [[ ! -x $solver_binary ]]; then
    echo "missing executable solver binary: $solver_binary" >&2
    exit "$EX_NOINPUT"
fi
solver_binary=$(realpath "$solver_binary")

case_id=$(
    python3 - "$case_json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    case = json.load(handle)
try:
    case_id = case["case_id"]
    run_control = case["run_control"]
    if not isinstance(case_id, str) or not case_id:
        raise ValueError("case_id must be a non-empty string")
    if run_control["type"] != "transient":
        raise ValueError("case is not transient")
    dt = float(run_control["time_step"])
    final_time = float(run_control["final_time"])
    if not (dt > 0.0 and final_time > dt):
        raise ValueError("invalid transient time controls")
except (KeyError, TypeError, ValueError) as error:
    raise SystemExit(f"invalid transient case JSON: {error}")
print(case_id)
PY
) || exit "$EX_USAGE"

# The launcher supplies --case, --output, and --report-level itself.  Resume
# must discard initial-condition-only options, because the solver rejects them
# together with --resume.
resume_options=()
for ((index = 0; index < ${#initial_options[@]}; ++index)); do
    option=${initial_options[index]}
    case "$option" in
        --case|--output|--report-level|--resume)
            echo "supervisor owns $option; do not pass it as an initial option" >&2
            exit "$EX_USAGE"
            ;;
        --restart|--restart-perturbation)
            if (( index + 1 >= ${#initial_options[@]} )); then
                echo "missing value after $option" >&2
                exit "$EX_USAGE"
            fi
            ((++index))
            ;;
        *)
            resume_options+=("$option")
            ;;
    esac
done

case_sha=$(sha256sum "$case_json" | awk '{print $1}')
solver_sha=$(sha256sum "$solver_binary" | awk '{print $1}')
options_sha=$(printf '%s\0' "${initial_options[@]}" | sha256sum | awk '{print $1}')

mkdir -p "$(dirname "$output_dir")"
# An advisory lock releases automatically when a systemd kill tears down this
# wrapper.  The persistent lock *file* is harmless; only its held flock means
# a live duplicate supervisor exists.
exec 9>"$lock_file"
if ! flock -n 9; then
    echo "another transient supervisor already owns: $output_dir" >&2
    exit "$EX_CANTCREAT"
fi
if ps -eo args= | awk -v output="$output_dir" \
    'index($0, "cfd_solver") && index($0, "--output " output) { found = 1 } END { exit !found }'; then
    echo "a cfd_solver already advertises this output directory: $output_dir" >&2
    exit "$EX_CANTCREAT"
fi

event() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >>"$event_file"
}

write_state() {
    local temporary
    umask 077
    temporary=$(mktemp "${state_file}.tmp.XXXXXX")
    {
        printf 'format=1\n'
        printf 'case_path=%s\n' "$case_json"
        printf 'case_sha256=%s\n' "$case_sha"
        printf 'case_id=%s\n' "$case_id"
        printf 'solver_path=%s\n' "$solver_binary"
        printf 'solver_sha256=%s\n' "$solver_sha"
        printf 'output_path=%s\n' "$output_dir"
        printf 'mpi_ranks=%s\n' "$mpi_ranks"
        printf 'first_cpu=%s\n' "$first_cpu"
        printf 'initial_options_sha256=%s\n' "$options_sha"
        printf 'initial_launch_recorded=true\n'
    } >"$temporary"
    mv -f "$temporary" "$state_file"
}

state_value() {
    local key=$1
    awk -F= -v key="$key" '$1 == key {sub(/^[^=]*=/, ""); print; found=1; exit} END {if (!found) exit 1}' "$state_file"
}

validate_state() {
    local key expected actual
    [[ -f $state_file ]] || return 1
    while (( $# )); do
        key=$1
        expected=$2
        shift 2
        actual=$(state_value "$key") || return 1
        [[ $actual == "$expected" ]] || return 1
    done
}

validate_status() {
    # Prints terminal kind: completed or failed.  A completed package needs
    # matching completion metadata; an explicit failed run_status is terminal
    # on its own so a partial metadata write cannot turn it into a retry.
    python3 - "$case_id" "$mpi_ranks" "$output_dir/run_status.json" "$output_dir/metadata.json" <<'PY'
import json
import math
import os
import sys

case_id, expected_ranks, status_path, metadata_path = sys.argv[1:]
expected_ranks = int(expected_ranks)
try:
    with open(status_path, encoding="utf-8") as handle:
        status = json.load(handle)
    if not isinstance(status, dict) or status.get("case_id") != case_id:
        raise ValueError("case_id does not match")
    if not isinstance(status.get("final_step"), int) or isinstance(status["final_step"], bool) or status["final_step"] < 0:
        raise ValueError("final_step is invalid")
    if not isinstance(status.get("final_physical_time"), (int, float)) or not math.isfinite(status["final_physical_time"]):
        raise ValueError("final_physical_time is invalid")
    if not isinstance(status.get("mpi_ranks"), int) or isinstance(status["mpi_ranks"], bool) or status["mpi_ranks"] != expected_ranks:
        raise ValueError("mpi_ranks does not match this supervisor")
    if not isinstance(status.get("command"), str) or not status["command"]:
        raise ValueError("command is missing")
    if not isinstance(status.get("notes"), str) or not status["notes"]:
        raise ValueError("notes are missing")
    kind = status.get("convergence_status")
    if kind == "failed":
        print("failed")
        raise SystemExit(0)
    if kind not in {"converged", "statistically_periodic"}:
        raise ValueError("convergence_status is not terminal")
    with open(metadata_path, encoding="utf-8") as handle:
        metadata = json.load(handle)
    if not isinstance(metadata, dict) or metadata.get("case_id") != case_id:
        raise ValueError("metadata case_id does not match")
    if metadata.get("completed") is not True or metadata.get("convergence_status") != kind:
        raise ValueError("metadata does not confirm completion")
    print("completed")
except (OSError, json.JSONDecodeError, ValueError, TypeError) as error:
    raise SystemExit(f"invalid terminal run_status: {error}")
PY
}

validate_resume_package() {
    python3 - "$case_json" "$output_dir" <<'PY'
import json
import math
import os
import sys

case_path, output = sys.argv[1:]
checkpoint = os.path.join(output, "transient_checkpoint.bin")
manifest_path = os.path.join(output, "transient_checkpoint.json")
required = ("stdout.log", "residuals.csv", "forces.csv", "surface.csv", "partition_diagnostics.csv")
try:
    with open(case_path, encoding="utf-8") as handle:
        case = json.load(handle)
    control = case["run_control"]
    case_id = case["case_id"]
    dt = float(control["time_step"])
    final_time = float(control["final_time"])
    if control["type"] != "transient" or not (dt > 0 and final_time > dt):
        raise ValueError("case is not a valid transient case")
    if os.path.exists(os.path.join(output, "metadata.json")) or os.path.exists(os.path.join(output, "run_status.json")):
        raise ValueError("package has terminal metadata or status")
    if not os.path.isfile(checkpoint) or not os.path.isfile(manifest_path):
        raise ValueError("durable checkpoint and manifest are both required")
    for name in required:
        if not os.path.isfile(os.path.join(output, name)):
            raise ValueError(f"missing required unfinished artifact: {name}")
    with open(checkpoint, "rb") as handle:
        if handle.read(8) != b"CFDTRN01":
            raise ValueError("checkpoint magic is invalid")
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    if not isinstance(manifest, dict) or manifest.get("format") != "CFDTRN01" or manifest.get("version") != 1 or manifest.get("case_id") != case_id:
        raise ValueError("checkpoint manifest case or format does not match")
    step = manifest.get("accepted_step")
    if not isinstance(step, int) or isinstance(step, bool) or step < 1:
        raise ValueError("checkpoint accepted_step is invalid")
    final_steps = round(final_time / dt)
    if not math.isclose(final_steps * dt, final_time, rel_tol=0.0, abs_tol=1e-10) or step >= final_steps:
        raise ValueError("checkpoint is not an unfinished transient state")
    if not isinstance(manifest.get("time_step"), (int, float)) or not math.isclose(float(manifest["time_step"]), dt, rel_tol=0.0, abs_tol=1e-12):
        raise ValueError("checkpoint time_step does not match case")
    if not isinstance(manifest.get("physical_time"), (int, float)) or not math.isclose(float(manifest["physical_time"]), step * dt, rel_tol=0.0, abs_tol=1e-10):
        raise ValueError("checkpoint physical_time does not match accepted_step")
    if manifest.get("force_history_samples") != step or manifest.get("inner_iteration_samples") != step:
        raise ValueError("checkpoint history counts do not match accepted_step")
except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
    raise SystemExit(f"unsafe resume package: {error}")
PY
}

terminal_status() {
    local terminal
    terminal=$(validate_status) || {
        echo "refusing unsafe terminal status in $output_dir/run_status.json" >&2
        event "refused invalid_terminal_status"
        exit "$EX_UNAVAILABLE"
    }
    event "terminal_${terminal}"
    echo "transient package is terminal: $terminal" >&2
    exit 0
}

if [[ -e $state_file ]]; then
    if ! validate_state format 1 case_path "$case_json" case_sha256 "$case_sha" case_id "$case_id" \
        solver_path "$solver_binary" solver_sha256 "$solver_sha" output_path "$output_dir" \
        mpi_ranks "$mpi_ranks" first_cpu "$first_cpu" initial_options_sha256 "$options_sha" \
        initial_launch_recorded true; then
        echo "supervisor state does not match this requested launch; refusing: $state_file" >&2
        event "refused state_mismatch"
        exit "$EX_UNAVAILABLE"
    fi
elif [[ -e $output_dir ]]; then
    echo "refusing unmanaged output path (initial launch is not provably unique): $output_dir" >&2
    event "refused unmanaged_output"
    exit "$EX_UNAVAILABLE"
else
    write_state
    event "initial_launch_recorded"
fi

if [[ -f $output_dir/run_status.json ]]; then
    terminal_status
fi

if [[ -e $output_dir ]]; then
    if ! validate_resume_package; then
        echo "refusing partial output without a matching durable checkpoint: $output_dir" >&2
        event "refused unsafe_resume_package"
        exit "$EX_UNAVAILABLE"
    fi
    event "launch resume checkpoint=$output_dir/transient_checkpoint.bin"
    set +e
    "$launcher" "$mpi_ranks" "$first_cpu" "$case_json" "$output_dir" \
        "${resume_options[@]}" --resume "$output_dir/transient_checkpoint.bin"
    launcher_status=$?
    set -e
else
    event "launch initial"
    set +e
    "$launcher" "$mpi_ranks" "$first_cpu" "$case_json" "$output_dir" "${initial_options[@]}"
    launcher_status=$?
    set -e
fi

event "launcher_exit_status=$launcher_status"
if [[ -f $output_dir/run_status.json ]]; then
    terminal_status
fi

if (( launcher_status == 0 )); then
    echo "launcher exited successfully without a terminal run_status; refusing retry" >&2
    event "refused successful_launcher_without_status"
    exit "$EX_UNAVAILABLE"
fi
if ! validate_resume_package; then
    echo "launcher failed without a safe durable checkpoint; refusing retry" >&2
    event "refused failed_launcher_without_safe_checkpoint"
    exit "$EX_UNAVAILABLE"
fi

event "retryable_interruption exit_status=$launcher_status"
# Normalize any retryable child failure to one status so systemd can suppress
# permanent configuration/safety refusals while restarting only interruptions.
exit "$EX_TEMPFAIL"

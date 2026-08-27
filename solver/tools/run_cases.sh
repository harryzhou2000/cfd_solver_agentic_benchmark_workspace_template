#!/usr/bin/env bash
# cns2d -- production run driver for the benchmark case suite.
#
# Runs every required case with the parameters supplied in its JSON file (no
# overrides), writing one result directory per case under results/.
#
# usage:
#   tools/run_cases.sh [-n <ranks>] [-o <results-dir>] [case_id ...]
#
# With no case ids, all eight required cases are run.  Each case writes
# <results>/<case_id>/ plus a launch log <results>/<case_id>.launch.log.
set -uo pipefail

SOLVER_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCHMARK_DIR="${CNS2D_BENCHMARK_DIR:-$(cd "${SOLVER_ROOT}/../cfd_solver_agentic_benchmark" && pwd)}"
CASES_DIR="${BENCHMARK_DIR}/inputs/cases"
EXECUTABLE="${CNS2D_EXECUTABLE:-${SOLVER_ROOT}/build/cns2d}"

RANKS=8
RESULTS_DIR="${SOLVER_ROOT}/results"

while getopts "n:o:h" opt; do
  case "${opt}" in
    n) RANKS="${OPTARG}" ;;
    o) RESULTS_DIR="${OPTARG}" ;;
    h)
      echo "usage: $0 [-n ranks] [-o results-dir] [case_id ...]"
      exit 0
      ;;
    *)
      echo "unknown option" >&2
      exit 2
      ;;
  esac
done
shift $((OPTIND - 1))

ALL_CASES=(
  naca0012_m015_inviscid
  naca0012_m080_inviscid
  naca0012_m200_inviscid
  naca0012_m015_laminar_re5000
  naca0012_m080_laminar_re5000
  naca0012_m200_laminar_re5000
  cylinder_m010_laminar_re20
  cylinder_m010_laminar_re200
)

if [ "$#" -gt 0 ]; then
  CASES=("$@")
else
  CASES=("${ALL_CASES[@]}")
fi

if [ ! -x "${EXECUTABLE}" ]; then
  echo "error: solver executable not found or not executable: ${EXECUTABLE}" >&2
  echo "build it first, e.g.:  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j" >&2
  exit 1
fi

mkdir -p "${RESULTS_DIR}"

status=0
for case_id in "${CASES[@]}"; do
  case_file="${CASES_DIR}/${case_id}.json"
  if [ ! -f "${case_file}" ]; then
    echo "error: case file not found: ${case_file}" >&2
    status=1
    continue
  fi
  out_dir="${RESULTS_DIR}/${case_id}"
  echo "=== ${case_id} (np=${RANKS}) -> ${out_dir}"
  mpirun --allow-run-as-root -np "${RANKS}" "${EXECUTABLE}" solve \
    --case "${case_file}" --output "${out_dir}" --log-every 250 \
    > "${RESULTS_DIR}/${case_id}.launch.log" 2>&1
  rc=$?
  if [ "${rc}" -ne 0 ]; then
    echo "  FAILED with exit status ${rc}; see ${RESULTS_DIR}/${case_id}.launch.log" >&2
    status="${rc}"
  else
    echo "  completed"
  fi
done

exit "${status}"

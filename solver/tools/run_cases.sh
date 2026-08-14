#!/usr/bin/env bash
set -euo pipefail

# Reproducible production launcher. One case is run at a time to keep MPI
# ranks isolated and make wall-clock/diagnostic records unambiguous.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CASES="$ROOT/../cfd_solver_agentic_benchmark/inputs/cases"
EXE="$ROOT/build/cfd2d"
RANKS="${1:-8}"
OUTROOT="${2:-$ROOT/results}"
mkdir -p "$OUTROOT"
export OMPI_ALLOW_RUN_AS_ROOT=1 OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

cases=(
  naca0012_m015_inviscid
  naca0012_m080_inviscid
  naca0012_m200_inviscid
  naca0012_m015_laminar_re5000
  naca0012_m080_laminar_re5000
  naca0012_m200_laminar_re5000
  cylinder_m010_laminar_re20
  cylinder_m010_laminar_re200
)

for id in "${cases[@]}"; do
  casefile="$CASES/$id.json"
  out="$OUTROOT/$id"
  mkdir -p "$out"
  echo "[$(date -Is)] starting $id np=$RANKS"
  /usr/bin/time -p mpirun --oversubscribe -np "$RANKS" "$EXE" solve \
    --case "$casefile" --output "$out" --report-level full \
    >"$out/launcher.stdout" 2>&1 || {
      rc=$?
      echo "[$(date -Is)] $id exited $rc" | tee -a "$out/launcher.stdout"
      continue
    }
  echo "[$(date -Is)] finished $id"
done

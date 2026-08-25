#!/usr/bin/env bash
# Launch one benchmark case with production settings.
# Usage: run_case.sh <case_id> [extra solver args...]
# Env: NP (default 8), LIMITER (default venkat).
set -eo pipefail

CASE_ID="$1"; shift || true
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
NPVAL="8"
if [ -n "$NP" ]; then NPVAL="$NP"; fi
LIM="venkat"
if [ -n "$LIMITER" ]; then LIM="$LIMITER"; fi

mkdir -p "$ROOT/results/$CASE_ID"
exec mpirun --bind-to none -np "$NPVAL" "$ROOT/build/cfd2d" solve \
  --case "$CASES/$CASE_ID.json" \
  --output "$ROOT/results/$CASE_ID" \
  --limiter "$LIM" --flux roe \
  "$@" 2>&1 | grep -vE 'Authorization required'

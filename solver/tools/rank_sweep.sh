#!/usr/bin/env bash
# Sequential rank-count sweep for the report's MPI scaling evidence.
# Runs naca0012_m015_inviscid and cylinder_m010_laminar_re20 at np=1,2,4,8
# with production numerics, back to back, so every timing is taken under the
# same macroscopic machine-load window. Timings on this shared host are
# indicative, not HPC-grade; force consistency is the primary claim.
set -eo pipefail
cd "$(dirname "$0")/.."
CASES=/workspace/cfd_solver_agentic_benchmark/inputs/cases
for CID in naca0012_m015_inviscid cylinder_m010_laminar_re20; do
  for NP in 1 2 4 8; do
    OUT="results/rankstudy/"$CID"_np"$NP
    find "$OUT" -mindepth 1 -not -path '*/partitions*' -delete 2>/dev/null || true
    mpirun --bind-to none -np "$NP" ./build/cfd2d solve --case "$CASES/"$CID".json" --output "$OUT" --limiter venkat --flux roe > "$OUT".log 2>&1 || true
    WALL=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["wall_time_seconds"])' "$OUT"/run_status.json 2>/dev/null || echo NA)
    LOAD=$(awk '{print $1}' /proc/loadavg)
    echo "done "$CID" np="$NP" solver_wall="$WALL" loadavg_at_finish="$LOAD >> results/rankstudy/sweep.log
  done
done
echo ALL_RANK_SWEEPS_DONE >> results/rankstudy/sweep.log

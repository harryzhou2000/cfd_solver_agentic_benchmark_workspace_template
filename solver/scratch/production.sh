#!/usr/bin/env bash
# Final production schedule.
#
# The container has a hard cgroup CPU quota of 4 CPUs
# (/sys/fs/cgroup/cpu.max = "400000 100000") even though nproc reports 64.
# Concurrent jobs therefore only split the same 4 CPUs and every stream runs
# slower with no throughput gain, so all cases run STRICTLY SEQUENTIALLY at the
# measured optimum of np=4.
cd /workspace/solver || exit 1
LOG=scratch/production_progress.txt
: > "$LOG"

# The seven steady cases first: they are individually short, so finishing them
# early makes their results available for figures and the report while the long
# transient runs afterwards.
echo "=== steady cases start $(date -u +%H:%M:%S)" >> "$LOG"
tools/run_cases.sh -n 4 \
  cylinder_m010_laminar_re20 \
  naca0012_m015_inviscid \
  naca0012_m080_inviscid \
  naca0012_m200_inviscid \
  naca0012_m015_laminar_re5000 \
  naca0012_m080_laminar_re5000 \
  naca0012_m200_laminar_re5000 \
  >> "$LOG" 2>&1
echo "=== steady cases done $(date -u +%H:%M:%S)" >> "$LOG"

# The Re 200 transient last, alone, with the whole quota.
echo "=== transient start $(date -u +%H:%M:%S)" >> "$LOG"
tools/run_cases.sh -n 4 cylinder_m010_laminar_re200 >> "$LOG" 2>&1
echo "=== transient done $(date -u +%H:%M:%S)" >> "$LOG"
echo ALL_DONE >> "$LOG"

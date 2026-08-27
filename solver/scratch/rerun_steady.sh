#!/usr/bin/env bash
# Re-run the seven steady cases under the corrected convergence criteria and the
# corrected wall-pressure force integral.  np=2 so the in-flight Re 200 transient
# (np=4) keeps most of the 4-CPU quota; these cases are individually short.
cd /workspace/solver || exit 1
LOG=scratch/rerun_progress.txt
: > "$LOG"
echo "=== rerun start $(date -u +%H:%M:%S) binary $(md5sum build/cns2d | cut -c1-16)" >> "$LOG"
tools/run_cases.sh -n 2 \
  cylinder_m010_laminar_re20 \
  naca0012_m015_inviscid \
  naca0012_m080_inviscid \
  naca0012_m200_inviscid \
  naca0012_m015_laminar_re5000 \
  naca0012_m080_laminar_re5000 \
  naca0012_m200_laminar_re5000 \
  >> "$LOG" 2>&1
echo "=== rerun done $(date -u +%H:%M:%S)" >> "$LOG"
echo RERUN_DONE >> "$LOG"

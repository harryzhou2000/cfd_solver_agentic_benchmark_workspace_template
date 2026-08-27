#!/bin/bash
# Stop every solver run and orchestration script started by this repository.
# Patterns are matched against full command lines, so this file is executed as a
# script rather than typed at a prompt: a shell whose own command line contains
# the pattern would otherwise kill itself.
for pat in 'run_everything' 'run_all' 'run_verify_extra' 'run_cfl_study' \
           'run_case' 'build/cfd2d' 'mpirun' 'orterun'; do
  pgrep -f "$pat" | while read -r p; do
    [ "$p" = "$$" ] && continue
    kill -9 "$p" 2>/dev/null
  done
done
echo "remaining solver processes: $(pgrep -cf 'build/cfd2d' || true)"
exit 0

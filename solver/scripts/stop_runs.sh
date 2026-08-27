#!/bin/bash
# Stop every solver run and orchestration script started by this repository.
for pat in 'build/cfd2d solve' 'orterun' 'mpirun -np' 'scripts/run_all.sh' \
           'scripts/run_case.sh' 'scripts/_master.sh' 'latexmk' 'pdflatex'; do
  pgrep -f "$pat" | while read -r p; do kill -9 "$p" 2>/dev/null; done
done
echo "remaining: $(pgrep -fc 'build/cfd2d' 2>/dev/null || echo 0)"

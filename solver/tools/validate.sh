#!/bin/bash
# Validate all results against the output contract.
set -e
cd /mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark/codex_glm52_03/solver
BENCH=../cfd_solver_agentic_benchmark
PYTHON=.venv/bin/python

echo "=== Validating case outputs ==="
RESULTS=""
for d in results/*/; do
    if [ -f "$d/metadata.json" ]; then
        RESULTS="$RESULTS $d"
    fi
done
$PYTHON $BENCH/examiner/validate_outputs.py $RESULTS --report report 2>&1 || true

echo ""
echo "=== Generating figures ==="
$PYTHON tools/plot_results.py

echo ""
echo "=== Generating sanity checks ==="
$PYTHON tools/gen_sanity_checks.py

echo ""
echo "=== Generating run manifest ==="
$PYTHON tools/gen_manifest.py

echo ""
echo "=== Generating report table ==="
$PYTHON tools/gen_report.py

echo ""
echo "=== Compiling LaTeX report ==="
cd report
pdflatex -interaction=nonstopmode report.tex 2>&1 | tail -5 || true
pdflatex -interaction=nonstopmode report.tex 2>&1 | tail -5 || true
echo "=== Done ==="

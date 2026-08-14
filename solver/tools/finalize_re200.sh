#!/usr/bin/env bash
# Finalize the Re200 production result and complete the report package.
# Run after the _re200_final solve process has exited.
set -eu
WS="$(cd "$(dirname "$0")/.." && pwd)"
CASES="$WS/../cfd_solver_agentic_benchmark/inputs/cases"
cd "$WS"

if pgrep -f "_re200_final" > /dev/null; then
  echo "re200 production still running; aborting finalize" >&2
  exit 1
fi

# promote
rm -rf results/cylinder_m010_laminar_re200
mv results/_re200_final results/cylinder_m010_laminar_re200
cd results/cylinder_m010_laminar_re200
ls field_t*.vtu 2>/dev/null | grep -v field_final | xargs -r rm -f
cd "$WS"
cp results/_re200_final.log results/cylinder_m010_laminar_re200/stdout.log 2>/dev/null || true

# figures + sanity + manifest
.venv/bin/python tools/make_figures_case.py results/cylinder_m010_laminar_re200 report/figures "$CASES/cylinder_m010_laminar_re200.json"
.venv/bin/python tools/sanity_checks.py results "$CASES" report/sanity_checks.json
.venv/bin/python tools/make_run_manifest.py results "$CASES" report
.venv/bin/python tools/re200_section.py

# report
cd report
pdflatex -interaction=nonstopmode report.tex > /dev/null
pdflatex -interaction=nonstopmode report.tex > /dev/null
cd "$WS"

# validation
V=.venv/bin/python
for c in naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
         naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 \
         naca0012_m200_laminar_re5000 cylinder_m010_laminar_re20 \
         cylinder_m010_laminar_re200; do
  $V ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py results/$c
done
$V ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py --report report \
  results/cylinder_m010_laminar_re200
echo "FINALIZE COMPLETE"

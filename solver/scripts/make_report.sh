#!/bin/bash
# Regenerates every report artefact from the submitted solver outputs.
set -uo pipefail
cd "$(dirname "$0")/.."
PY=.venv/bin/python
R=results
REP=report

rm -f $REP/figures/*.png
set -x
# Order-of-accuracy / freestream / linear-exactness verification.
source scripts/env.sh
mpirun -np 4 $MPIRUN_FLAGS ./build/cfd2d verify --levels 4 --base 16 \
    --case $CASES/naca0012_m015_laminar_re5000.json --out $REP/verification.json \
    2>/dev/null | tee $REP/verification.log
$PY tools/mpi_study.py --root studies/mpi --out-csv $REP/mpi_study.csv \
    --out-figure $REP/figures/mpi_scaling.png || true
$PY tools/analyze_transient.py --case-dir $R/cylinder_m010_laminar_re200 \
    --case-json $CASES/cylinder_m010_laminar_re200.json \
    --out-figure $REP/figures/cylinder_re200_shedding.png \
    --out-json $REP/shedding_analysis.json --start-fraction 0.5 > /dev/null || true
# The Mach 0.8 case is the one where limiter freezing is decisive: it converges
# within a few steps of the freeze point, whereas the unfrozen run limit-cycles.
$PY tools/limiter_study.py --frozen $R/naca0012_m080_inviscid \
    --free studies/verify/naca0012_m080_inviscid_nofreeze --freeze-step 9000 \
    --case-label "NACA0012 \$M_\\infty=0.8\$" \
    --out $REP/figures/limiter_study.png || true
$PY tools/plot_verification.py --json $REP/verification.json \
    --out $REP/figures/mms_order.png || true
$PY tools/make_figures.py --results $R --out $REP/figures --manifest $REP/figure_manifest.csv
$PY tools/sanity_checks.py --results $R --manifest $REP/figure_manifest.csv \
    --out $REP/sanity_checks.json
sanity=$?
$PY tools/run_manifest.py --results $R --out $REP/run_manifest.csv \
    --extra $(ls -d studies/mpi/*/ studies/verify/*/ 2>/dev/null | tr '\n' ' ')
$PY tools/make_report_tables.py --results $R --report $REP
set +x
if [ "${sanity:-0}" -ne 0 ]; then
  echo "WARNING: physics sanity gate reported failures (see $REP/sanity_checks.json)"
fi

cd $REP && latexmk -pdf -interaction=nonstopmode -halt-on-error report.tex > latexmk.log 2>&1
status=$?
cd ..
if [ $status -eq 0 ]; then echo "report/report.pdf built"; else
  echo "LaTeX build FAILED; see report/latexmk.log"; tail -40 $REP/latexmk.log; fi
echo MAKE_REPORT_DONE

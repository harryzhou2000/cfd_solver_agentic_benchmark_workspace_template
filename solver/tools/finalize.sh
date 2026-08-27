#!/usr/bin/env bash
# Finalization pipeline: regenerate all figures, manifests, sanity checks, MPI
# section, report PDF, and run the examiner validator. Reproducible end-to-end.
set -e
cd "$(dirname "$0")/.."
PY=.venv/bin/python
CASES="naca0012_m015_inviscid naca0012_m080_inviscid naca0012_m200_inviscid \
naca0012_m015_laminar_re5000 naca0012_m080_laminar_re5000 naca0012_m200_laminar_re5000 \
cylinder_m010_laminar_re20 cylinder_m010_laminar_re200"
CSVS=$(echo $CASES | tr ' ' ',')

echo "== [1/8] per-case figures =="
for c in $CASES; do
  $PY tools/plot_case.py --case-dir results/$c --out-dir report/figures || echo "WARN plot $c"
done

echo "== [2/8] figure manifest =="
$PY tools/make_figure_manifest.py --figures-dir report/figures --out report/figure_manifest.csv

echo "== [3/8] run manifest + summary + Strouhal =="
$PY tools/build_report_data.py --results-root results --report-dir report --cases $CSVS

echo "== [4/8] sanity checks =="
$PY tools/sanity_check.py --results-root results --out report/sanity_checks.json \
    --figures-dir report/figures --manifest report/figure_manifest.csv

echo "== [5/8] MPI section =="
$PY tools/build_mpi_section.py --naca-tag naca_m080 --cyl-tag cyl_re20

echo "== [6/8] report latex =="
$PY tools/fill_report.py --report-dir report --analysis-json report/analysis.json

echo "== [7/8] compile PDF =="
cd report && pdflatex -interaction=nonstopmode -halt-on-error report.tex >/dev/null && \
             pdflatex -interaction=nonstopmode -halt-on-error report.tex >/dev/null
cd ..

echo "== [8/8] examiner validation =="
OUTS=$(for c in $CASES; do echo -n "results/$c "; done)
$PY ../cfd_solver_agentic_benchmark/examiner/validate_outputs.py $OUTS --report report

echo "== FINALIZATION COMPLETE =="

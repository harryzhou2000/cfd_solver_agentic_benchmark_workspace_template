#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/generate_report.py --strict
latexmk -pdf -interaction=nonstopmode -halt-on-error -outdir=report report/report.tex

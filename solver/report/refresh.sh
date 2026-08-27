#!/bin/sh
# Regenerate the report's numbers and rebuild the PDF.
#
#   ./refresh.sh
#
# The cutoff below excludes results produced by the build whose steady
# termination test was defective (see the report subsection "A defect in the
# steady termination test").  Those runs stopped while their force histories
# were still moving, so quoting them as final results would be wrong.  Any case
# whose run_status.json predates the cutoff is reported as still in progress.
#
# Raise the cutoff, never lower it, when further re-runs supersede earlier ones.
set -e

CNS_TRUSTED_AFTER="2026-08-27 07:58:00"
cd "$(dirname "$0")"

CNS_HARVEST_MIN_MTIME="$(date -d "$CNS_TRUSTED_AFTER" +%s)" \
  ../.venv/bin/python harvest_numbers.py

CNS_HARVEST_MIN_MTIME="$(date -d "$CNS_TRUSTED_AFTER" +%s)" \
  ../.venv/bin/python make_artifacts.py

latexmk -pdf -interaction=nonstopmode report.tex > /dev/null 2>&1 || true

printf 'pdflatex errors: %s\n' "$(grep -ac '^!' report.log || true)"
printf 'undefined refs:  %s\n' "$(grep -ac 'Reference.*undefined' report.log || true)"
grep -ao 'Output written on [^)]*)' report.log | tail -1

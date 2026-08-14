#!/usr/bin/env bash
# Final report/artifact assembly. Run after all eight final_* directories are
# complete: generates figures, manifests, sanity checks, the filled report
# tables, and the PDF.
set -euo pipefail

cd "$(dirname "$0")/.."

RESULTS=(results/final_*_np1 results/final_cylinder_m010_laminar_re200_np3)

for d in "${RESULTS[@]}"; do
    [ -d "$d" ] || continue
    case_id=$(basename "$d")
    .venv/bin/python tools/plot_histories.py "$d"
    .venv/bin/python tools/plot_surface.py "$d"
    case "$case_id" in
        *naca*)
            .venv/bin/python tools/plot_fields.py "$d" --zoom 1.6 ;;
        *re20*)
            .venv/bin/python tools/plot_fields.py "$d" --zoom 4 ;;
        *re200*)
            .venv/bin/python tools/plot_fields.py "$d" --zoom 12 ;;
    esac
done

.venv/bin/python tools/make_manifests.py results
.venv/bin/python tools/make_sanity_checks.py results
.venv/bin/python tools/finalize_report.py results

echo "final artifacts written under report/"

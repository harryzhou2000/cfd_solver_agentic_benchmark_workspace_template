#!/bin/bash
# Full submission sweep, in the order the report artefacts depend on:
#   steady cases -> MPI rank study -> cross-checks -> dual-time parameter study
#   -> the long Reynolds 200 transient.
# Run scripts/make_report.sh afterwards to regenerate the report.
set -uo pipefail
cd "$(dirname "$0")/.."
echo "=== STEADY ===";       scripts/run_all.sh steady
echo "=== MPI STUDY ===";    scripts/run_all.sh mpi
echo "=== VERIFY RUNS ===";  scripts/run_all.sh verify
echo "=== VERIFY EXTRA ==="; scripts/run_verify_extra.sh
echo "=== CFL STUDY ===";    scripts/run_cfl_study.sh
echo "=== TRANSIENT ===";    scripts/run_all.sh transient
echo RUN_EVERYTHING_DONE

#!/usr/bin/env bash
set -euo pipefail

solver=${1:?solver executable is required}
case_json=${2:?case fixture is required}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

set +e
"$solver" solve --case "$case_json" --output "$temporary/run" --report-level full
solver_status=$?
set -e

# A one-step fixture cannot demonstrate a statistically periodic wake, so the
# production solver honestly returns its numerical-failure status after writing
# the accepted state.  The integration assertion here is the durable callback.
[[ $solver_status == 2 ]]

python3 - "$temporary/run" <<'PY'
import json
import sys
from pathlib import Path

run = Path(sys.argv[1])
manifest = json.loads((run / "transient_checkpoint.json").read_text())
status = json.loads((run / "run_status.json").read_text())
assert manifest["format"] == "CFDTRN01"
assert manifest["case_id"] == "transient_checkpoint_step1"
assert manifest["accepted_step"] == 1
assert abs(manifest["physical_time"] - 0.01) <= 1.0e-15
assert abs(manifest["time_step"] - 0.01) <= 1.0e-15
assert manifest["global_cell_count"] == 10185
assert manifest["force_history_samples"] == 1
assert manifest["inner_iteration_samples"] == 1
assert manifest["bdf_history"] == "previous_and_older_accepted_states"
assert (run / "transient_checkpoint.bin").stat().st_size > 0
assert status["final_step"] == 1
assert abs(status["final_physical_time"] - 0.01) <= 1.0e-15

residual_rows = (run / "residuals.csv").read_text().strip().splitlines()
force_rows = (run / "forces.csv").read_text().strip().splitlines()
assert len(residual_rows) == 2
assert len(force_rows) == 2
assert residual_rows[-1].split(",", 1)[0] == "1"
assert force_rows[-1].split(",", 1)[0] == "1"
assert "durable transient BDF2 checkpoint step=1" in (run / "stdout.log").read_text()
PY

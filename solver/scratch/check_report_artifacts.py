"""Run only the examiner's report-artifact validation, without the case checks.

The case checks fail while a run is mid-write (a truncated final CSV row), which
is expected and unrelated to the report artifacts this script exercises.
"""
import sys
from pathlib import Path

sys.path.insert(0, "/workspace/cfd_solver_agentic_benchmark/examiner")
import validate_outputs as v

case_ids = sys.argv[2:]
try:
    v.validate_report(Path(sys.argv[1]), case_ids)
    print("REPORT ARTIFACTS: OK for", case_ids)
except AssertionError as exc:
    print("REPORT ARTIFACTS FAIL:", exc)

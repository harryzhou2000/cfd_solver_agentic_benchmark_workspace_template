#!/usr/bin/env python3
"""Gate the transient result before anything downstream consumes it.

Checks the completed cylinder Re200 case against every requirement that is
specific to it, and confirms its provenance matches the submitted binary.
Exit status 0 only when all of them pass.  Read-only.
"""
import csv
import json
import re
import sys
from pathlib import Path

D = Path("results/cylinder_m010_laminar_re200")
REV = "cc42ab4facfd"
CASE = json.loads(Path("/workspace/cfd_solver_agentic_benchmark/inputs/cases/"
                       "cylinder_m010_laminar_re200.json").read_text())
rc = CASE["run_control"]

fails = []


def check(ok, msg):
    print(("  ok   " if ok else "  FAIL ") + msg)
    if not ok:
        fails.append(msg)


meta = json.loads((D / "metadata.json").read_text())
st = json.loads((D / "run_status.json").read_text())

print("provenance")
check(meta.get("git_revision") == REV,
      f"metadata git_revision = {meta.get('git_revision')!r}, want {REV!r}")
check(meta.get("mpi_ranks") == 4, f"metadata mpi_ranks = {meta.get('mpi_ranks')!r}, want 4")
check(st.get("mpi_ranks") == 4, f"run_status mpi_ranks = {st.get('mpi_ranks')!r}, want 4")
check(meta.get("completed") is True, f"completed = {meta.get('completed')!r}")

print("transient completion")
check(st.get("final_step") >= 30000, f"final_step = {st.get('final_step')!r}, want >= 30000")
check(abs(st.get("final_physical_time", 0) - rc["final_time"]) < 1e-6,
      f"final_physical_time = {st.get('final_physical_time')!r}, want {rc['final_time']}")
check(st.get("convergence_status") == "statistically_periodic",
      f"convergence_status = {st.get('convergence_status')!r}")
check(meta.get("convergence_status") == st.get("convergence_status"),
      "metadata and run_status disagree on convergence_status")

print("BDF2 inner loop and supplied controls")
check(meta.get("true_bdf2_inner_loop") is True,
      f"true_bdf2_inner_loop = {meta.get('true_bdf2_inner_loop')!r}")
check(meta.get("inner_residual_reduction_target") == rc["inner_residual_reduction_target"],
      f"inner target = {meta.get('inner_residual_reduction_target')!r}")
check(meta.get("min_inner_iterations") == rc["min_inner_iterations"],
      f"min_inner_iterations = {meta.get('min_inner_iterations')!r}")
check(meta.get("max_inner_iterations") == rc["max_inner_iterations"],
      f"max_inner_iterations = {meta.get('max_inner_iterations')!r}")
frac = meta.get("inner_target_converged_fraction", 0.0)
check(frac >= 0.95, f"inner_target_converged_fraction = {frac!r}, want >= 0.95")
lo = meta.get("observed_min_inner_iterations")
hi = meta.get("observed_max_inner_iterations")
check(lo is not None and lo >= rc["min_inner_iterations"],
      f"observed_min_inner_iterations = {lo!r}")
check(hi is not None and hi <= rc["max_inner_iterations"],
      f"observed_max_inner_iterations = {hi!r}")

print("force history sampled by physical time")
steps, times = [], []
with (D / "forces.csv").open() as fh:
    for row in csv.DictReader(fh):
        try:
            steps.append(int(float(row["step"]))); times.append(float(row["physical_time"]))
        except (ValueError, TypeError):
            continue
check(len(steps) >= 30000, f"{len(steps)} force rows, want >= 30000")
check(abs(times[-1] - rc["final_time"]) < 1e-6, f"last force time = {times[-1]!r}")
dts = [times[i + 1] - times[i] for i in range(len(times) - 1)]
worst = max(abs(d - rc["time_step"]) for d in dts)
check(worst < 1e-9, f"worst dt deviation = {worst:.3e}, want < 1e-9")
incr = set(steps[i + 1] - steps[i] for i in range(len(steps) - 1))
check(incr == {1}, f"step increments = {sorted(incr)}, want exactly one row per physical step")
check(steps[-1] == st.get("final_step"),
      f"last force step {steps[-1]} != run_status final_step {st.get('final_step')}")

print("unsteady lift, required by the sanity gate")
cls = []
with (D / "forces.csv").open() as fh:
    for row in csv.DictReader(fh):
        try:
            t = float(row["physical_time"]); c = float(row["cl"])
        except (ValueError, TypeError):
            continue
        if t >= 150.0:
            cls.append(c)
check(len(cls) > 1000 and (max(cls) - min(cls)) > 0.1,
      f"C_L variation after t=150 is {max(cls)-min(cls):.4f}, want > 0.1")

print("field output")
check((D / "field_final.vtu").is_file(), "field_final.vtu present")
txt = (D / "field_final.vtu").read_text()
check(txt.count("<Piece") == 1, f"{txt.count('<Piece')} Piece elements")
types = re.search(r'Name="types"[^>]*>(.*?)</DataArray>', txt, re.S)
if types:
    tset = sorted(set(int(v) for v in types.group(1).split()))
    check(all(v in (5, 9) for v in tset), f"cell types {tset}")

print("run completion in the log")
log = (D / "stdout.log").read_text()
check("run finished" in log.lower() or st.get("final_step") >= 30000,
      "stdout.log does not record a finished run")

print()
if fails:
    print(f"{len(fails)} CHECK(S) FAILED")
    sys.exit(1)
print("ALL TRANSIENT CHECKS PASSED")

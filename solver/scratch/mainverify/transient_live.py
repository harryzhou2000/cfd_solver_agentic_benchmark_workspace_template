#!/usr/bin/env python3
"""Check the in-flight transient against the supplied production controls.

Uses only the live forces.csv / residuals.csv / stdout.log, so it can run while
the case is still marching.  Verifies the things that must hold regardless of
how far the run has got: the physical time step, that forces are sampled by
physical time rather than by inner iteration, that the inner loop stays inside
the requested 5..1000 range and meets the 1e-3 target, and that the shedding is
already a saturated limit cycle.  Read-only.
"""
import csv
import json
import math
import re
from pathlib import Path

D = Path("results/cylinder_m010_laminar_re200")
CASE = json.loads(Path("/workspace/cfd_solver_agentic_benchmark/inputs/cases/"
                       "cylinder_m010_laminar_re200.json").read_text())
rc = CASE["run_control"]
print("requested controls:", {k: rc[k] for k in
      ("type", "time_step", "final_time", "min_inner_iterations",
       "max_inner_iterations", "inner_residual_reduction_target") if k in rc})

steps, times, cd, cl = [], [], [], []
with (D / "forces.csv").open() as fh:
    for row in csv.DictReader(fh):
        try:
            s = int(float(row["step"])); t = float(row["physical_time"])
            c = float(row["cd"]); l = float(row["cl"])
        except (ValueError, TypeError, KeyError):
            continue  # partial final row: the run is writing right now
        steps.append(s); times.append(t); cd.append(c); cl.append(l)

print(f"force rows: {len(steps)}  step {steps[0]}..{steps[-1]}  t {times[0]:.3f}..{times[-1]:.3f}")
dts = [times[i + 1] - times[i] for i in range(len(times) - 1)]
print(f"physical dt: min {min(dts):.6e} max {max(dts):.6e} "
      f"(requested {rc['time_step']})")
bad = [d for d in dts if abs(d - rc["time_step"]) > 1e-9]
print(f"rows deviating from the requested dt: {len(bad)}")
dstep = set(steps[i + 1] - steps[i] for i in range(len(steps) - 1))
print(f"step increments present: {sorted(dstep)}  -> one force row per physical step"
      if dstep == {1} else f"step increments: {sorted(dstep)}")

# Inner-iteration statistics from the log.
inner, ratios = [], []
for line in (D / "stdout.log").read_text().splitlines():
    m = re.search(r"inner\s+(\d+)\s+\(ratio\s+([0-9.eE+-]+)\)", line)
    if m:
        inner.append(int(m.group(1))); ratios.append(float(m.group(2)))
if inner:
    print(f"logged physical steps: {len(inner)}")
    print(f"inner iterations: min {min(inner)} max {max(inner)} "
          f"mean {sum(inner)/len(inner):.1f}  (allowed {rc['min_inner_iterations']}"
          f"..{rc['max_inner_iterations']})")
    print(f"  outside the allowed range: "
          f"{sum(1 for v in inner if v < rc['min_inner_iterations'] or v > rc['max_inner_iterations'])}")
    tgt = rc["inner_residual_reduction_target"]
    met = sum(1 for r in ratios if r <= tgt)
    print(f"inner target {tgt}: met on {met}/{len(ratios)} logged steps "
          f"({100.0*met/len(ratios):.1f} %), worst ratio {max(ratios):.3e}")

# Is the limit cycle saturated?  Compare successive non-overlapping windows.
print("\nshedding amplitude by window (non-overlapping, 20 time units):")
w = 20.0
t0 = math.floor(times[0])
edges = []
lo = max(t0, 40.0)
while lo + w <= times[-1]:
    edges.append((lo, lo + w)); lo += w
for a, b in edges:
    seg = [(c, l) for t, c, l in zip(times, cd, cl) if a <= t < b]
    if len(seg) < 50:
        continue
    cds = [x[0] for x in seg]; cls = [x[1] for x in seg]
    print(f"  t {a:6.1f}-{b:6.1f}  n={len(seg):5d}  "
          f"cd mean {sum(cds)/len(cds):.6f} p-p {max(cds)-min(cds):.6f}   "
          f"cl mean {sum(cls)/len(cls):+.6f} amp {(max(cls)-min(cls))/2:.6f}")

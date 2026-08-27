"""Verify sec_sensitivity.tex against the variant run outputs directly.

Chain has two hops:  run output -> RESULTS.md -> sec_sensitivity.tex
This script reads the RUN OUTPUTS (hop-1 source of truth) so both hops can be
judged.  Read-only over scratch/sensitivity/.
"""
import csv
import json
import os

RUNS = "/workspace/solver/scratch/sensitivity/runs"

VARIANTS = [
    "floor005", "floor001", "floor00", "floor05",
    "order1", "order2", "venk1", "venk10",
    "naca_floor005", "naca_floor00",
    "inv_floor005", "inv_floor00",
    "_stale_naca_floor005_killed_attempt",
]


def jload(variant, name):
    path = os.path.join(RUNS, variant, name)
    if not os.path.exists(path):
        return None
    with open(path) as fh:
        return json.load(fh)


def forces(variant):
    path = os.path.join(RUNS, variant, "forces.csv")
    if not os.path.exists(path):
        return []
    with open(path, newline="") as fh:
        out = []
        for row in csv.DictReader(fh):
            out.append({k: (float(v) if v not in (None, "") else None)
                        for k, v in row.items()})
        return out


print("%-38s %-14s %-26s %8s %10s %14s %14s %14s"
      % ("variant", "git_rev", "case", "ranks", "steps", "cd_last",
         "pdrag_last", "vdrag_last"))
for v in VARIANTS:
    md = jload(v, "metadata.json") or {}
    st = jload(v, "run_status.json") or {}
    f = forces(v)
    last = f[-1] if f else {}
    print("%-38s %-14s %-26s %8s %10s %14s %14s %14s"
          % (v, md.get("git_revision"), md.get("case_id"),
             md.get("mpi_ranks"), st.get("final_step"),
             ("%.6f" % last["cd"]) if last.get("cd") is not None else "-",
             ("%.6f" % last["pressure_drag"]) if last.get("pressure_drag") is not None else "-",
             ("%.6f" % last["viscous_drag"]) if last.get("viscous_drag") is not None else "-"))

print()
print("=== detail: orders / status / wall time / controls")
for v in VARIANTS:
    md = jload(v, "metadata.json") or {}
    st = jload(v, "run_status.json") or {}
    rd = md.get("run_details", {})
    print("%-38s status=%-14s orders=%-10s steps=%-8s wall=%-10s"
          % (v, st.get("convergence_status"),
             ("%.4f" % st["residual_reduction_orders"]) if st.get("residual_reduction_orders") is not None else "-",
             st.get("final_step"),
             ("%.2f" % st["wall_time_seconds"]) if st.get("wall_time_seconds") is not None else "-"))
    interesting = {k: val for k, val in rd.items()
                   if any(t in k for t in ("floor", "venk", "order", "limiter",
                                           "max_steps", "spatial", "dissip"))}
    extra = {k: val for k, val in md.items()
             if any(t in k for t in ("floor", "venk", "order", "limiter",
                                     "spatial", "dissip", "reconstruction"))}
    if interesting or extra:
        print("        controls: %s %s" % (json.dumps(interesting), json.dumps(extra)))

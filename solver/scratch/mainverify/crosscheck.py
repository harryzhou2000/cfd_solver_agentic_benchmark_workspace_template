#!/usr/bin/env python3
"""Cross-check harvested report macros against the submitted result files.

Independent of harvest_numbers.py: parses numbers_auto.tex, then re-derives each
quantity straight from results/ and compares.  Read-only.
"""
import csv
import json
import re
import sys
from pathlib import Path

REPORT = Path("report")
RESULTS = Path("results")

macros = {}
for line in (REPORT / "numbers_auto.tex").read_text().splitlines():
    m = re.match(r"\\def\\(cns[A-Za-z]+)\{(.*)\}\s*$", line)
    if m:
        macros[m.group(1)] = m.group(2)


def num(v):
    """Strip \num{...} / \pm and return a float when possible."""
    if v is None:
        return None
    s = re.sub(r"\\num\{([^}]*)\}", r"\1", v)
    s = s.replace("\\,", "").replace("$", "").replace("\\%", "").strip()
    try:
        return float(s)
    except ValueError:
        return None


SUFFIX = {
    "CylSteady": "cylinder_m010_laminar_re20",
    "NacaSubInv": "naca0012_m015_inviscid",
    "NacaTraInv": "naca0012_m080_inviscid",
    "NacaSupInv": "naca0012_m200_inviscid",
    "NacaSubLam": "naca0012_m015_laminar_re5000",
    "NacaTraLam": "naca0012_m080_laminar_re5000",
    "NacaSupLam": "naca0012_m200_laminar_re5000",
}

fails, checked = [], 0


def cmp(name, got, want, tol, label):
    global checked
    if got is None or want is None:
        return
    checked += 1
    if abs(got - want) > tol * max(1.0, abs(want)):
        fails.append(f"{name}: report {got!r} vs results {want!r}  ({label})")


for suf, case in SUFFIX.items():
    d = RESULTS / case
    st = json.loads((d / "run_status.json").read_text())
    meta = json.loads((d / "metadata.json").read_text())
    rows = [r for r in csv.DictReader((d / "forces.csv").open())]
    last = rows[-1]
    cmp(f"cnsSteps{suf}", num(macros.get(f"cnsSteps{suf}")), float(st["final_step"]), 0, "final_step")
    cmp(f"cnsOrders{suf}", num(macros.get(f"cnsOrders{suf}")),
        st["residual_reduction_orders"], 5e-3, "residual orders")
    cmp(f"cnsCd{suf}", num(macros.get(f"cnsCd{suf}")), float(last["cd"]), 5e-4, "final cd")
    cmp(f"cnsCl{suf}", num(macros.get(f"cnsCl{suf}")), float(last["cl"]), 5e-3, "final cl")
    cmp(f"cnsWall{suf}", num(macros.get(f"cnsWall{suf}")),
        st["wall_time_seconds"], 1e-2, "wall time")
    cmp(f"cnsMpiRanks{suf}", num(macros.get(f"cnsMpiRanks{suf}")),
        float(meta["mpi_ranks"]), 0, "mpi ranks")
    cmp(f"cnsPartitionEdgeCut{suf}", num(macros.get(f"cnsPartitionEdgeCut{suf}")),
        float(meta["partition_edge_cut"]), 0, "edge cut")
    cmp(f"cnsNumCellsGlobal{suf}", num(macros.get(f"cnsNumCellsGlobal{suf}")),
        float(meta["num_cells_global"]), 0, "cells")

# transient
d = RESULTS / "cylinder_m010_laminar_re200"
st = json.loads((d / "run_status.json").read_text())
meta = json.loads((d / "metadata.json").read_text())
cmp("cnsShedSteps", num(macros.get("cnsShedSteps")), float(st["final_step"]), 0, "steps")
for key, mk in (("inner_target_converged_fraction", "cnsInnerTargetConvergedFractionShed"),
                ("observed_min_inner_iterations", "cnsObservedMinInnerIterationsShed"),
                ("observed_max_inner_iterations", "cnsObservedMaxInnerIterationsShed")):
    if mk in macros:
        cmp(mk, num(macros[mk]), float(meta[key]), 1e-6, key)

print(f"macros defined: {len(macros)}")
print(f"quantities cross-checked against results/: {checked}")
print(f"mismatches: {len(fails)}")
for f in fails:
    print("  MISMATCH " + f)
n_pending = sum(1 for v in macros.values() if "pending" in v)
print(f"macros still defined as pending: {n_pending}")
sys.exit(1 if fails else 0)

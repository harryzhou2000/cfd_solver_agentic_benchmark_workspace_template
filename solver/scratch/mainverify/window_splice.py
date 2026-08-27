#!/usr/bin/env python3
"""Test report_author's objection to my amplification window.

Claim: for best-state-restore cases the restored row is APPENDED after the
superseded march, so "the last 500 rows of the file" splices rows from an
abandoned trajectory onto the reported state.  The correct window is the 500
rows ending at the reported step.  Read-only.
"""
import csv
import json
from pathlib import Path

CASES = [
    "cylinder_m010_laminar_re20",
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
]


def rows(case):
    out = []
    with (Path("results") / case / "forces.csv").open() as fh:
        for r in csv.DictReader(fh):
            try:
                out.append((int(float(r["step"])), float(r["cd"])))
            except (ValueError, TypeError):
                continue
    return out


def ladder(cd):
    n = len(cd) // 10
    means = [sum(cd[i * n:(i + 1) * n]) / n for i in range(10)]
    dec = [means[i + 1] - means[i] for i in range(9)]
    rs = [abs(dec[i + 1]) / abs(dec[i]) for i in range(8) if abs(dec[i]) > 0]
    r = sum(rs) / len(rs)
    mono = all(d > 0 for d in dec) or all(d < 0 for d in dec)
    return r, dec[-1], mono


print("case                             reported  file_last  restored?  "
      "r(file tail)  r(ending at reported)  rem 1/(1-r)   rem r/(1-r)")
for case in CASES:
    rs_ = rows(case)
    st = json.loads((Path("results") / case / "run_status.json").read_text())
    reported = st["final_step"]
    file_last = rs_[-1][0]
    # index of the LAST occurrence of the reported step
    idx = max(i for i, (s, _) in enumerate(rs_) if s == reported)
    # was the reported state appended after a longer march?
    marched_past = max(s for s, _ in rs_)
    restored = marched_past > reported
    tail_file = [c for _, c in rs_[-500:]]
    # window ending at the reported state, taken from the march itself
    upto = [c for s, c in rs_[:idx] if s <= reported]
    tail_rep = upto[-500:]
    r1, d1, m1 = ladder(tail_file)
    r2, d2, m2 = ladder(tail_rep) if len(tail_rep) >= 100 else (float("nan"),) * 3
    rem_a = abs(d2) / (1 - r2) if r2 < 1 else float("inf")
    rem_b = abs(d2) * r2 / (1 - r2) if r2 < 1 else float("inf")
    print(f"{case:32s} {reported:8d} {file_last:10d}  {str(restored):9s}  "
          f"{r1:12.4f}  {r2:21.4f}  {rem_a:11.3e}  {rem_b:11.3e}"
          + ("" if m2 else "  NON-MONO"))

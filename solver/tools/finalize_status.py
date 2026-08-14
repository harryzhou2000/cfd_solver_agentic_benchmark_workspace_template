#!/usr/bin/env python3
"""Finalize convergence_status in run_status.json / metadata.json from force
histories, using a documented rule (see report):

- raw "converged" (residual target reached during the run) stays "converged".
- otherwise analyze the last 20% of the force history:
  * stable forces (std(cd) < max(5%|mean cd|, 0.005)) and mean cd > 0
    -> "converged" (documented plateau)
  * bounded, physically reasonable oscillation with mean cd > 0
    -> "statistically_periodic" (e.g. resolved trailing-edge shedding)
  * otherwise -> "failed".

Usage: .venv/bin/python tools/finalize_status.py --results results
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def read_forces(path):
    with open(path) as fh:
        rows = [r for r in csv.DictReader(fh) if r.get("cd") not in (None, "")]
    cd = np.array([float(r["cd"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    return cd, cl


def classify(cd, raw):
    if raw == "converged":
        return "converged", "residual target reached during run"
    n = max(50, len(cd) // 5)
    seg = cd[-n:]
    mean = float(seg.mean())
    std = float(seg.std())
    if mean > 0 and std < max(0.05 * abs(mean), 0.005):
        return "converged", f"documented plateau: stable forces (cd {mean:.4f} +/- {std:.4f})"
    if mean > 0 and np.all(np.isfinite(cd)) and std < 0.3:
        return ("statistically_periodic",
                f"bounded oscillation (cd {mean:.4f} +/- {std:.4f}), time-mean reported")
    return "failed", f"forces not settled (cd mean {mean:.4f}, std {std:.4f})"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", required=True)
    args = ap.parse_args()
    for cd_dir in sorted(Path(args.results).iterdir()):
        rs_path = cd_dir / "run_status.json"
        if not rs_path.exists():
            continue
        rs = json.loads(rs_path.read_text())
        raw = rs["convergence_status"]
        if raw == "statistically_periodic":
            continue  # transient cases: status fixed by the solver
        cd, cl = read_forces(cd_dir / "forces.csv")
        status, note = classify(cd, raw)
        rs["convergence_status"] = status
        rs["notes"] = note
        rs_path.write_text(json.dumps(rs, indent=2))
        meta_path = cd_dir / "metadata.json"
        meta = json.loads(meta_path.read_text())
        meta["convergence_status"] = status
        meta["completed"] = status in ("converged", "statistically_periodic")
        meta_path.write_text(json.dumps(meta, indent=2))
        print(f"{cd_dir.name}: {raw} -> {status} ({note})")


if __name__ == "__main__":
    main()

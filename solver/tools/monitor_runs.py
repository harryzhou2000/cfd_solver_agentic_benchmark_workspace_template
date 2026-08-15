#!/usr/bin/env python3
"""Background monitor for Phase 5 production runs.

Appends per-case step progress (from stdout.log) and process liveness to
solver/tools/timing_work/monitor.log every --interval seconds.

Usage: python3 monitor_runs.py [--interval 300] [--forever]
"""

import argparse
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
RESULTS = REPO / "solver" / "results"
LOG = REPO / "solver" / "tools" / "timing_work" / "monitor.log"

CASES = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]


def live_cases() -> set[str]:
    out = subprocess.run(
        ["ps", "-eo", "args"], capture_output=True, text=True
    ).stdout
    return {
        c for c in CASES if any(
            f"{c}.json" in line and "cfd_solver" in line for line in out.splitlines()
        )
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--interval", type=float, default=300.0)
    ap.add_argument("--forever", action="store_true")
    args = ap.parse_args()

    LOG.parent.mkdir(parents=True, exist_ok=True)
    with LOG.open("a") as f:
        while True:
            stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            f.write(f"--- {stamp} ---\n")
            for c in CASES:
                log = RESULTS / c / "stdout.log"
                last = "no-log"
                if log.exists():
                    lines = [l for l in log.read_text().splitlines() if "step" in l]
                    last = lines[-1].strip() if lines else "no-step-line"
                f.write(f"{c:32s} {last}\n")
            f.write(f"alive: {sorted(live_cases())}\n")
            f.flush()
            if not args.forever:
                return 0
            time.sleep(args.interval)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Write report/run_manifest.md from run_status.json/metadata.json files."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"

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


def main() -> int:
    lines = [
        "# Run Manifest",
        "",
        "All runs use the cfd2d binary built from this repository (see",
        "README.md for the build command). Production runs use MPI ranks=8,",
        "Roe flux, and the Venkatakrishnan limiter. Exact commands are recorded",
        "below as executed (also stored per case in run_status.json).",
        "",
        "| case | ranks | steps | final time | residual orders | wall [s] | status | command extras |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for cid in CASES:
        d = RESULTS / cid
        st = json.loads((d / "run_status.json").read_text())
        cmd = st["command"]
        extras = ""
        for flag in ["--restart", "--cfl-max", "--residual-target",
                     "--pert-aoa-deg", "--pert-duration"]:
            if flag in cmd:
                parts = cmd.split(flag)[1].split()
                # Keep the full (relative) path so the restart source case
                # is unambiguous.
                extras += f" {flag} {parts[0]}"
        lines.append(
            f"| {cid} | {st['mpi_ranks']} | {st['final_step']} | "
            f"{st['final_physical_time']:.2f} | "
            f"{st['residual_reduction_orders']:.2f} | "
            f"{st['wall_time_seconds']:.1f} | {st['convergence_status']} |"
            f"{extras.strip()} |"
        )
    lines += [
        "",
        "Base command for every case:",
        "",
        "    mpirun --bind-to none -np 8 ./build/cfd2d solve --case <case>.json",
        "      --output results/<case_id> --limiter venkat --flux roe",
        "",
        "The Re 200 transient was initialized from a steady pseudo-converged",
        "state (cases/cylinder_m010_laminar_re200_steadyinit.json, a steady",
        "control deck over the identical physics/mesh) via --restart, plus the",
        "2 deg / 10-time-unit startup angle-of-attack perturbation.",
        "",
        "## Rank-count study",
        "",
        "results/rankstudy/<case>_np<N>/ holds np=1,2,4,8 reruns of",
        "naca0012_m015_inviscid and cylinder_m010_laminar_re20 with identical",
        "numerics, executed back-to-back in one machine-load window by",
        "tools/rank_sweep.sh. The np=8 sweep runs reproduce the production",
        "np=8 runs exactly (identical step counts and final forces). Timings",
        "and force spreads are in report/sanity_checks.json (mpi_rank_study).",
        "",
    ]
    out = ROOT / "report" / "run_manifest.md"
    out.write_text("\n".join(lines))
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

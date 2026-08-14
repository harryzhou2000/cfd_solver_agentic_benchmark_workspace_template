#!/usr/bin/env python3
"""Promote converged diagnostic directories to the canonical final_*_np1
layout. Only directories whose run_status says converged/statistically_periodic
and metadata says completed=true are copied."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"

PROMOTIONS = [
    ("probe_cyl_rus_freeze", "cylinder_m010_laminar_re20"),
    ("probe_m015lam_frz", "naca0012_m015_laminar_re5000"),
    ("probe_m080lam_roe1", "naca0012_m080_laminar_re5000"),
    ("probe_m200lam_roe1", "naca0012_m200_laminar_re5000"),
    ("prod_m080inv", "naca0012_m080_inviscid"),
    ("prod_m200inv_frz", "naca0012_m200_inviscid"),
    ("prod_m015inv_frz", "naca0012_m015_inviscid"),
]


def promote(src_name: str, case_id: str, np: int = 1) -> None:
    src = RESULTS / src_name
    dst = RESULTS / f"final_{case_id}_np{np}"
    if not (src / "run_status.json").exists():
        print(f"skip {src_name}: missing run_status")
        return
    status = json.loads((src / "run_status.json").read_text())
    metadata = json.loads((src / "metadata.json").read_text())
    if status.get("convergence_status") not in {"converged",
                                                "statistically_periodic"}:
        print(f"skip {src_name}: status={status.get('convergence_status')}")
        return
    if metadata.get("completed") is not True:
        print(f"skip {src_name}: metadata completed != true")
        return
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    print(f"promoted {src_name} -> {dst.name}")


def main() -> None:
    for src, case in PROMOTIONS:
        promote(src, case)


if __name__ == "__main__":
    main()

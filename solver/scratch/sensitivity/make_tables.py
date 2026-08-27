#!/usr/bin/env python3
"""Assemble the sensitivity-study tables from the probe run directories."""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_surface import analyze  # noqa: E402

SENS = "/workspace/solver/scratch/sensitivity"
RUNS = os.path.join(SENS, "runs")
BASELINE = "/workspace/solver/results/cylinder_m010_laminar_re20"


def status(directory):
    path = os.path.join(directory, "run_status.json")
    if not os.path.isfile(path):
        return {}
    with open(path) as handle:
        return json.load(handle)


def row(label, directory):
    result, _ = analyze(directory)
    st = status(directory)
    forces = result.get("forces", {}) or {}
    conv = result.get("convergence", {}) or {}
    surf = (result.get("surface", {}) or {}).get("cp", {}) or {}
    return {
        "label": label,
        "dir": directory,
        "exists": os.path.isdir(directory),
        "cd": forces.get("cd"),
        "pressure_drag": forces.get("pressure_drag"),
        "viscous_drag": forces.get("viscous_drag"),
        "cl": forces.get("cl"),
        "orders": st.get("residual_reduction_orders", conv.get("residual_reduction_orders")),
        "final_step": st.get("final_step", forces.get("step")),
        "wall_s": st.get("wall_time_seconds", conv.get("wall_time_seconds")),
        "status": st.get("convergence_status", conv.get("convergence_status")),
        "osc_ratio": surf.get("oscillation_ratio"),
        "alternations": surf.get("sign_alternations"),
        "max_d1": surf.get("max_abs_first_difference"),
        "max_d2": surf.get("max_abs_second_difference"),
        "asym": surf.get("symmetry_max_asymmetry"),
        "cp_min": surf.get("min"),
        "cp_max": surf.get("max"),
    }


def f(value, spec="{:.6f}"):
    if value is None:
        return "n/a"
    if isinstance(value, str):
        return value
    try:
        return spec.format(value)
    except (TypeError, ValueError):
        return str(value)


def emit(title, rows, columns):
    print()
    print("### " + title)
    print()
    header = "| " + " | ".join(c[0] for c in columns) + " |"
    sep = "|" + "|".join("---" for _ in columns) + "|"
    print(header)
    print(sep)
    for r in rows:
        cells = []
        for _, key, spec in columns:
            cells.append(f(r.get(key), spec) if spec else f(r.get(key)))
        print("| " + " | ".join(cells) + " |")


FORCE_COLS = [
    ("variant", "label", None),
    ("Cd", "cd", "{:.6f}"),
    ("pressure Cd", "pressure_drag", "{:.6f}"),
    ("viscous Cd", "viscous_drag", "{:.6f}"),
    ("Cl", "cl", "{:.3e}"),
    ("orders", "orders", "{:.2f}"),
    ("final step", "final_step", None),
    ("wall [s]", "wall_s", "{:.1f}"),
    ("status", "status", None),
]

SMOOTH_COLS = [
    ("variant", "label", None),
    ("cp range min", "cp_min", "{:.5f}"),
    ("cp range max", "cp_max", "{:.5f}"),
    ("max |d1|", "max_d1", "{:.4e}"),
    ("max |d2|", "max_d2", "{:.4e}"),
    ("d2/d1", "osc_ratio", "{:.3f}"),
    ("sign alt.", "alternations", None),
    ("max mirror asym.", "asym", "{:.3e}"),
]


def main():
    groups = {
        "TASK 1 - Roe linear-wave dissipation floor": [
            ("production baseline (floor 0.05, np=8)", BASELINE),
            ("floor 0.05 (rebuilt copy)", os.path.join(RUNS, "floor005")),
            ("floor 0.01", os.path.join(RUNS, "floor001")),
            ("floor 0.00", os.path.join(RUNS, "floor00")),
        ],
        "TASK 2 - spatial order": [
            ("spatial order 1", os.path.join(RUNS, "order1")),
            ("spatial order 2", os.path.join(RUNS, "order2")),
        ],
        "TASK 3 - Venkatakrishnan k": [
            ("venkat_k = 1.0", os.path.join(RUNS, "venk1")),
            ("venkat_k = 5.0 (default, = order 2 run)", os.path.join(RUNS, "order2")),
            ("venkat_k = 10.0", os.path.join(RUNS, "venk10")),
        ],
    }
    payload = {}
    for title, entries in groups.items():
        rows = [row(label, d) for label, d in entries]
        payload[title] = rows
        emit(title + " (forces / convergence)", rows, FORCE_COLS)
        emit(title + " (surface smoothness)", rows, SMOOTH_COLS)
    with open(os.path.join(SENS, "tables.json"), "w") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)


if __name__ == "__main__":
    main()


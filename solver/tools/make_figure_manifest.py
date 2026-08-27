#!/usr/bin/env python
"""Scan a figures directory and write report/figure_manifest.csv.

CSV header: figure_file,case_id,figure_type,variable,source_file,caption

Per-figure metadata is taken from figure_sources.json in the figures
directory when present (written by plot_case.py); otherwise it is inferred
from the file name. The naming rule is enforced: variable contains 'mach'
for *_mach*.png and 'pressure' for *_pressure*.png.
"""
import argparse
import csv
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# longest suffix first so mach_zoom is not parsed as mach
FIGURE_SUFFIXES = [
    ("vorticity_wake", "vorticity", None),  # source resolved below
    ("pressure_zoom", "pressure", "field_final.vtk"),
    ("mach_zoom", "mach", "field_final.vtk"),
    ("surface_cp", "cp", "surface.csv"),
    ("residual", "residual_l2", "residuals.csv"),
    ("forces", "cl,cd", "forces.csv"),
    ("mach", "mach", "field_final.vtk"),
    ("pressure", "pressure", "field_final.vtk"),
]

CAPTIONS = {
    "residual": "Residual history (L2 and per-equation).",
    "forces": "Lift and drag coefficient history.",
    "surface_cp": "Surface pressure coefficient distribution.",
    "mach": "Mach number field, full domain.",
    "mach_zoom": "Mach number field, near-body/wake zoom.",
    "pressure": "Pressure field, full domain.",
    "pressure_zoom": "Pressure field, near-body/wake zoom.",
    "vorticity_wake": "Post-transient vorticity wake field (clipped range).",
}


def infer_entry(fname):
    """Infer manifest fields from a figure file name."""
    stem = fname[:-4] if fname.endswith(".png") else fname
    for suffix, variable, source in FIGURE_SUFFIXES:
        tail = "_" + suffix
        if stem.endswith(tail):
            case_id = stem[: -len(tail)]
            return {
                "case_id": case_id,
                "figure_type": suffix,
                "variable": variable,
                "source_file": source or "fields/ (latest field_t*.vtk)",
                "caption": f"Case {case_id}: " + CAPTIONS.get(suffix, suffix),
            }
    return {
        "case_id": stem, "figure_type": "unknown", "variable": "unknown",
        "source_file": "unknown", "caption": f"Unrecognized figure {fname}.",
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--figures-dir", required=True)
    ap.add_argument("--out", required=True, help="output figure_manifest.csv")
    args = ap.parse_args()

    sources = {}
    src_json = os.path.join(args.figures_dir, "figure_sources.json")
    if os.path.isfile(src_json):
        with open(src_json) as f:
            sources = json.load(f)

    rows = []
    problems = []
    for fname in sorted(os.listdir(args.figures_dir)):
        if not fname.endswith(".png"):
            continue
        entry = dict(sources.get(fname) or infer_entry(fname))
        entry.setdefault("figure_type", "unknown")
        # enforce the naming rule on the *figure type*, never the case id,
        # and report violations instead of silently rewriting them
        ftype = entry["figure_type"]
        if ftype in ("mach", "mach_zoom") and "mach" not in entry["variable"].lower():
            problems.append(f"{fname}: variable '{entry['variable']}' lacks 'mach'")
        if ftype in ("pressure", "pressure_zoom") \
                and "pressure" not in entry["variable"].lower():
            problems.append(f"{fname}: variable '{entry['variable']}' lacks 'pressure'")
        rows.append({
            "figure_file": fname,
            "case_id": entry.get("case_id", ""),
            "figure_type": entry["figure_type"],
            "variable": entry.get("variable", ""),
            "source_file": entry.get("source_file", ""),
            "caption": entry.get("caption", ""),
        })

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["figure_file", "case_id",
                                          "figure_type", "variable",
                                          "source_file", "caption"])
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {args.out} ({len(rows)} figures)")
    for p in problems:
        print("WARNING:", p)
    if problems:
        sys.exit(1)


if __name__ == "__main__":
    main()

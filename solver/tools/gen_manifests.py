#!/usr/bin/env python3
"""Generate report/run_manifest.csv and report/figure_manifest.csv.

Usage:
    python3 tools/gen_manifests.py results/naca_test results/naca_lam ...
    python3 tools/gen_manifests.py --auto          # one best dir per case_id

run_manifest columns:
    case_id, result_dir, mpi_ranks, convergence_status, final_step,
    residual_reduction, final_cl, final_cd, wall_time

figure_manifest columns:
    figure_file, case_id, figure_type, variable, source_file, caption
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402

RUN_MANIFEST_COLS = [
    "case_id", "result_dir", "mpi_ranks", "convergence_status", "final_step",
    "residual_reduction", "final_cl", "final_cd", "wall_time",
]
FIG_MANIFEST_COLS = [
    "figure_file", "case_id", "figure_type", "variable", "source_file", "caption",
]

IMAGE_EXT = (".png", ".pdf", ".jpg", ".jpeg", ".svg", ".eps")

# (regex on lowercased stem, variable, figure_type, source_file)
# order matters: cp before pressure; vort/velocity before mach.
FIG_RULES = [
    (r"cp", "Pressure coefficient (Cp)", "surface_cp", "surface.csv"),
    (r"vort", "Vorticity", "field_contour", "field_final.vtu"),
    (r"velocity|velmag|vmag|umag|speed", "Velocity", "field_contour", "field_final.vtu"),
    (r"mach", "Mach", "field_contour", "field_final.vtu"),
    (r"pressure|press|\bp\b", "Pressure", "field_contour", "field_final.vtu"),
    (r"residual|res_|resid", "Residual (L2)", "residual_history", "residuals.csv"),
    (r"force|lift|drag|\bcl\b|\bcd\b|\bcm\b", "Force coefficients", "force_history", "forces.csv"),
    (r"mesh", "Mesh", "mesh", "mesh_file"),
    (r"partition|part_", "Partition", "partition", "partition_diagnostics.csv"),
]


def classify_figure(stem: str):
    low = stem.lower()
    for pat, var, ftype, src in FIG_RULES:
        if re.search(pat, low):
            return var, ftype, src
    return "Unknown", "other", "unknown"


def case_tokens(case_dirs):
    """(token, case_id) pairs sorted longest-token-first for prefix matching."""
    pairs = []
    seen = set()
    for d, cid in case_dirs:
        base = os.path.basename(os.path.normpath(d))
        for tok in (base, cid):
            if tok and tok not in seen:
                pairs.append((tok, cid))
                seen.add(tok)
    pairs.sort(key=lambda x: len(x[0]), reverse=True)
    return pairs


def match_case(stem, tokens):
    for tok, cid in tokens:
        if stem.startswith(tok) and (len(stem) == len(tok) or stem[len(tok)] in "_-. "):
            return cid
    return ""


def caption_for(var, ftype, cid):
    label = f"case {cid}" if cid else "the case"
    if ftype == "residual_history":
        return f"Pseudo/physical-time residual history for {label}."
    if ftype == "force_history":
        return f"Force-coefficient history ($C_L$/$C_D$) for {label}."
    if ftype == "surface_cp":
        return f"Surface pressure-coefficient distribution for {label}."
    if ftype == "field_contour":
        return f"{var} field contour for {label}."
    if ftype == "partition":
        return f"Mesh partition / halo map for {label}."
    if ftype == "mesh":
        return f"Computational mesh for {label}."
    return f"{var} figure for {label}."


def build_run_manifest(result_dirs, out_path):
    rows = []
    for d in result_dirs:
        loaded = C.load_result(d)
        if not loaded:
            C.warn(f"skipping (incomplete metadata/run_status): {d}")
            continue
        meta, stat = loaded
        cid = meta.get("case_id", os.path.basename(os.path.normpath(d)))
        fr = C.final_force_row(os.path.join(d, "forces.csv"))
        rel = os.path.relpath(d, C.SOLVER_ROOT)
        rows.append({
            "case_id": cid,
            "result_dir": rel,
            "mpi_ranks": meta.get("mpi_ranks", stat.get("mpi_ranks", "")),
            "convergence_status": meta.get("convergence_status",
                                           stat.get("convergence_status", "")),
            "final_step": stat.get("final_step", ""),
            "residual_reduction": stat.get("residual_reduction_orders", ""),
            "final_cl": fr.get("cl", "") if fr else "",
            "final_cd": fr.get("cd", "") if fr else "",
            "wall_time": stat.get("wall_time_seconds", ""),
        })
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=RUN_MANIFEST_COLS)
        w.writeheader()
        w.writerows(rows)
    return rows


def build_figure_manifest(case_dirs, fig_dir, out_path):
    tokens = case_tokens(case_dirs)
    figs = []
    if os.path.isdir(fig_dir):
        figs = sorted(f for f in os.listdir(fig_dir)
                      if f.lower().endswith(IMAGE_EXT))
    rows = []
    for fn in figs:
        stem = os.path.splitext(fn)[0]
        cid = match_case(stem, tokens)
        var, ftype, src = classify_figure(stem)
        rows.append({
            "figure_file": fn,
            "case_id": cid,
            "figure_type": ftype,
            "variable": var,
            "source_file": src,
            "caption": caption_for(var, ftype, cid),
        })
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIG_MANIFEST_COLS)
        w.writeheader()
        w.writerows(rows)
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("result_dirs", nargs="*", help="result directories (one per case)")
    ap.add_argument("--auto", action="store_true",
                    help="auto-discover the best complete dir per case_id")
    ap.add_argument("--results-dir", default=C.RESULTS_DIR)
    ap.add_argument("--report-dir", default=C.REPORT_DIR)
    ap.add_argument("--figures-dir", default=None)
    args = ap.parse_args(argv)

    report_dir = args.report_dir
    figures_dir = args.figures_dir or os.path.join(report_dir, "figures")
    os.makedirs(report_dir, exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    if args.auto:
        best = C.best_dir_per_case(args.results_dir)
        dirs = list(best.values())
    else:
        dirs = [os.path.abspath(d) for d in args.result_dirs]
    if not dirs:
        ap.error("no result directories given (pass dirs or --auto)")

    # keep order stable / de-duplicate
    seen = set()
    clean = []
    for d in dirs:
        d = os.path.abspath(d)
        if d not in seen:
            seen.add(d)
            clean.append(d)
    dirs = clean

    case_dirs = []
    for d in dirs:
        loaded = C.load_result(d)
        cid = loaded[0].get("case_id", os.path.basename(d)) if loaded else os.path.basename(d)
        case_dirs.append((d, cid))

    rm = os.path.join(report_dir, "run_manifest.csv")
    fm = os.path.join(report_dir, "figure_manifest.csv")
    run_rows = build_run_manifest(dirs, rm)
    fig_rows = build_figure_manifest(case_dirs, figures_dir, fm)

    print(f"wrote {rm} ({len(run_rows)} cases)")
    print(f"wrote {fm} ({len(fig_rows)} figures)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Summarize the rank-count validation runs into report/rankcheck_table.tex
and report/rankcheck_summary.md."""
import csv
import glob
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RANKROOT = os.path.join(ROOT, "results_rankcheck")
REPORT = os.path.join(ROOT, "report")

CASES = ["naca0012_m015_inviscid", "cylinder_m010_laminar_re20"]


def last_force(case, np):
    p = os.path.join(RANKROOT, f"{case}_np{np}", "forces.csv")
    if not os.path.exists(p):
        return None
    rows = list(csv.DictReader(open(p)))
    return rows[-1]


def wall_time(case, np):
    p = os.path.join(RANKROOT, f"{case}_np{np}", "run_status.json")
    if not os.path.exists(p):
        return None
    return json.load(open(p)).get("wall_time_seconds")


def main():
    tex_rows = []
    md_rows = []
    for case in CASES:
        vals = []
        for np in (1, 2, 4, 8):
            r = last_force(case, np)
            if r is None:
                continue
            wt = wall_time(case, np)
            vals.append(r)
            tex_rows.append(
                f"{case.replace('_', '\\_')} & {np} & {int(float(r['step']))} "
                f"& {float(r['cd']):.6f} & {float(r['cl']):.6f} "
                f"& {wt:.0f} s \\\\")
            md_rows.append(
                f"| {case} | {np} | {int(float(r['step']))} | {float(r['cd']):.6f} "
                f"| {float(r['cl']):.6f} | {wt:.0f} s |")
        if len(vals) == 4:
            cd = [float(r["cd"]) for r in vals]
            cl = [float(r["cl"]) for r in vals]
            tex_rows.append(
                f"\\multicolumn{{6}}{{l}}{{\\em max deviation vs np=8: "
                f"$\\Delta C_D={max(abs(c - cd[-1]) for c in cd):.2e}$, "
                f"$\\Delta C_L={max(abs(c - cl[-1]) for c in cl):.2e}$}} \\\\")
            md_rows.append(
                f"| {case} | all | -- | max $\\Delta C_D$ "
                f"{max(abs(c - cd[-1]) for c in cd):.2e} | max $\\Delta C_L$ "
                f"{max(abs(c - cl[-1]) for c in cl):.2e} |")

    header = (
        "% Rank-count validation: force coefficients at the common final step\n"
        "\\begin{table}[ht]\n\\centering\n"
        "\\caption{Rank-count validation: final force coefficients at a common "
        "pseudo-time step for np=1, 2, 4, 8.}\n\\label{tab:rankcheck}\n"
        "\\begin{tabular}{lrrrrr}\n\\toprule\n"
        "Case & Ranks & Steps & $C_D$ & $C_L$ & Time \\\\\n\\midrule\n"
    )
    footer = "\\bottomrule\n\\end{tabular}\n\\end{table}\n"
    with open(os.path.join(REPORT, "rankcheck_table.tex"), "w") as f:
        f.write(header + "\n".join(tex_rows) + "\n" + footer)

    with open(os.path.join(REPORT, "rankcheck_summary.md"), "w") as f:
        f.write("# Rank-Count Validation Summary\n\n")
        f.write("| case | ranks | steps | cd | cl | wall time |\n"
                "|---|---|---|---|---|---|\n")
        f.write("\n".join(md_rows) + "\n")
    print("wrote rankcheck_table.tex and rankcheck_summary.md")


if __name__ == "__main__":
    main()

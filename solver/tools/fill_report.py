#!/usr/bin/env python3
"""Fill report.tex placeholders (%CASE_SECTIONS%, %MPI_SECTION%) with
data-driven LaTeX generated from results_summary.json and report/figures.

Idempotent: regenerating after runs complete refreshes all numbers. The prose
per case is hand-written here; numbers are injected from the summary so the
report stays consistent with the submitted CSV/field outputs.
"""
import argparse, json, os, sys
from pathlib import Path

CASE_ORDER = [
    "naca0012_m015_inviscid", "naca0012_m080_inviscid", "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000", "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
]
TITLES = {
    "naca0012_m015_inviscid": "NACA0012 inviscid, $M_\\infty=0.15$",
    "naca0012_m080_inviscid": "NACA0012 inviscid, $M_\\infty=0.80$",
    "naca0012_m200_inviscid": "NACA0012 inviscid, $M_\\infty=2.0$",
    "naca0012_m015_laminar_re5000": "NACA0012 laminar, $M_\\infty=0.15$, $Re=5000$",
    "naca0012_m080_laminar_re5000": "NACA0012 laminar, $M_\\infty=0.80$, $Re=5000$",
    "naca0012_m200_laminar_re5000": "NACA0012 laminar, $M_\\infty=2.0$, $Re=5000$",
    "cylinder_m010_laminar_re20": "Cylinder laminar, $M_\\infty=0.1$, $Re=20$ (steady)",
    "cylinder_m010_laminar_re200": "Cylinder laminar, $M_\\infty=0.1$, $Re=200$ (vortex shedding)",
}

def fig(cid, kind, cap, width=0.62):
    return ("\\begin{figure}[H]\\centering\n"
            "\\includegraphics[width=%s\\linewidth]{figures/%s_%s.png}\n"
            "\\caption{%s}\\label{fig:%s_%s}\\end{figure}\n" % (width, cid, kind, cap, cid, kind))

def fmt(x, nd=4):
    try:
        return ("%%.%dg" % nd) % float(x)
    except Exception:
        return str(x)

def case_section(cid, e, figs):
    t = []
    t.append("\\subsection{%s}\\label{sec:%s}" % (TITLES.get(cid, cid), cid))
    status = e.get("convergence_status", "")
    orders = e.get("residual_reduction_orders", "")
    steps = e.get("final_step", "")
    cd = e.get("cd", ""); cl = e.get("cl", "")
    np_ = e.get("mpi_ranks", "")
    wall = e.get("wall_time_seconds", "")
    if e.get("strouhal") is not None:
        summ = ("\\noindent Status: \\textbf{%s}. Post-transient vortex shedding: "
                "Strouhal $St=%s$, mean $C_D=%s$, lift amplitude $\\Delta C_L=%s$. "
                "%s physical steps to $t=%s$ (np=%s, %.0f s).\n"
                % (status, fmt(e.get("strouhal"),4), fmt(e.get("mean_cd_posttransient"),4),
                   fmt(e.get("cl_amplitude"),4), steps, fmt(e.get("final_physical_time"),1),
                   np_, float(wall or 0)))
    else:
        summ = ("\\noindent Status: \\textbf{%s}. Final $C_D=%s$, $C_L=%s$, "
                "residual reduction %s orders over %s steps (np=%s, %.0f s).\n"
                % (status, fmt(cd,5), fmt(cl,5), orders, steps, np_, float(wall or 0)))
    t.append(summ)
    # residual + forces
    if "%s_residual.png" % cid in figs:
        t.append(fig(cid, "residual", "%s: residual history (per-equation L2 and global L2)." % TITLES.get(cid,cid), 0.55))
    if "%s_forces.png" % cid in figs:
        t.append(fig(cid, "forces", "%s: lift and drag coefficient history." % TITLES.get(cid,cid), 0.55))
    if "%s_surface_cp.png" % cid in figs:
        t.append(fig(cid, "surface_cp", "%s: surface pressure coefficient." % TITLES.get(cid,cid), 0.55))
    # field figures mach + pressure (full + zoom)
    block = []
    for kind, lab in [("mach","Mach"),("mach_zoom","Mach (zoom)"),("pressure","pressure"),("pressure_zoom","pressure (zoom)")]:
        if "%s_%s.png" % (cid, kind) in figs:
            block.append(fig(cid, kind, "%s: %s field." % (TITLES.get(cid,cid), lab), 0.49))
    t.extend(block)
    if "%s_vorticity_wake.png" % cid in figs:
        t.append(fig(cid, "vorticity_wake", "%s: post-transient vorticity wake (clipped to $[-5,5]$)." % TITLES.get(cid,cid), 0.7))
    # analysis placeholder filled below
    t.append("%%ANALYSIS:%s%%" % cid)
    return "\n".join(t)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report-dir", default="report")
    ap.add_argument("--analysis-json", default=None)
    args = ap.parse_args()
    rep = Path(args.report_dir)
    summary = json.load(open(rep / "results_summary.json"))
    figs = set(f for f in os.listdir(rep / "figures") if f.endswith(".png"))
    analysis = {}
    if args.analysis_json and os.path.exists(args.analysis_json):
        analysis = json.load(open(args.analysis_json))
    # build case sections
    parts = []
    for cid in CASE_ORDER:
        if cid not in summary:
            continue
        sec = case_section(cid, summary[cid], figs)
        prose = analysis.get(cid, "")
        sec = sec.replace("%%ANALYSIS:%s%%" % cid, prose)
        parts.append(sec)
    case_tex = "\n\n".join(parts)
    # summary results table
    rows = ["\\begin{table}[H]\\centering\\small",
            "\\begin{tabular}{lrrrrr}",
            "\\toprule",
            "Case & $C_D$ & $C_L$ & res.\\ orders & steps & status \\\\",
            "\\midrule"]
    for cid in CASE_ORDER:
        if cid not in summary: continue
        e = summary[cid]
        short = cid.replace("naca0012_","N-").replace("_","\\_").replace("cylinder_","cyl\\_")
        rows.append("%s & %s & %s & %s & %s & %s \\\\" % (
            short, fmt(e.get("cd",0),5), fmt(e.get("cl",0),5),
            e.get("residual_reduction_orders",""), e.get("final_step",""),
            e.get("convergence_status","")))
    rows += ["\\bottomrule", "\\end{tabular}",
             "\\caption{Final force coefficients, residual reduction, and convergence status for all cases.}",
             "\\label{tab:results}\\end{table}"]
    summary_tex = "\n".join(rows)
    out = {"case_tex": case_tex, "summary_tex": summary_tex}
    (rep / "generated_case_sections.tex").write_text(case_tex)
    (rep / "generated_summary_table.tex").write_text(summary_tex)
    print("wrote generated_case_sections.tex (%d cases) and generated_summary_table.tex" % len(parts))

if __name__ == "__main__":
    main()

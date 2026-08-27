#!/usr/bin/env python3
"""Generates report/generated_tables.tex and report/generated_macros.tex.

Every number quoted in the report comes from this script, i.e. directly from
the submitted CSV/JSON outputs, so the report cannot drift from the data.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import re
import sys

import numpy as np

# LaTeX / package commands that look like our macros but are not.
LATEX_BUILTINS = {
    "providecommand", "newcommand", "includegraphics", "graphicspath", "documentclass",
    "usepackage", "tableofcontents", "hspace", "vspace", "linewidth", "textwidth",
    "toprule", "midrule", "bottomrule", "maketitle", "subsection", "subsubsection",
    "tabular", "lstset", "detokenize", "IfFileExists", "parbox", "fbox", "mathbf",
    "mathrm", "mathcal", "textbf", "textit", "texttt", "textsc", "footnotesize",
    "clearpage", "newpage", "centering", "caption", "label", "cref", "Cref", "ref",
    "input", "section", "paragraph", "emph", "code", "fig", "gtable", "mathsf",
    "colon", "infty", "partial", "nabla", "cdot", "quad", "qquad", "hline",
}

CASE_DIR = os.environ.get("CFD_CASE_DIR",
                          "../cfd_solver_agentic_benchmark/inputs/cases")

CASE_ORDER = [
    "naca0012_m015_inviscid",
    "naca0012_m080_inviscid",
    "naca0012_m200_inviscid",
    "naca0012_m015_laminar_re5000",
    "naca0012_m080_laminar_re5000",
    "naca0012_m200_laminar_re5000",
    "cylinder_m010_laminar_re20",
    "cylinder_m010_laminar_re200",
]

PRETTY = {
    "naca0012_m015_inviscid": r"NACA0012 $M_\infty=0.15$ inviscid",
    "naca0012_m080_inviscid": r"NACA0012 $M_\infty=0.80$ inviscid",
    "naca0012_m200_inviscid": r"NACA0012 $M_\infty=2.0$ inviscid",
    "naca0012_m015_laminar_re5000": r"NACA0012 $M_\infty=0.15$ laminar $Re=5000$",
    "naca0012_m080_laminar_re5000": r"NACA0012 $M_\infty=0.80$ laminar $Re=5000$",
    "naca0012_m200_laminar_re5000": r"NACA0012 $M_\infty=2.0$ laminar $Re=5000$",
    "cylinder_m010_laminar_re20": r"cylinder $M_\infty=0.1$ laminar $Re=20$",
    "cylinder_m010_laminar_re200": r"cylinder $M_\infty=0.1$ laminar $Re=200$",
}

STATUS_TEX = {
    "converged": r"\textsc{converged}",
    "statistically_periodic": r"\textsc{stat.\ periodic}",
    "failed": r"\textbf{\textsc{failed}}",
}


def esc(s):
    return str(s).replace("_", r"\_").replace("%", r"\%")


def load_csv(path):
    rows = list(csv.DictReader(open(path, newline="")))
    out = {}
    for k in rows[0]:
        try:
            out[k] = np.array([float(r[k]) for r in rows])
        except ValueError:
            out[k] = np.array([r[k] for r in rows])
    return out


def fmt(x, n=4):
    if x is None:
        return "--"
    if isinstance(x, str):
        return esc(x)
    if x == 0:
        return "0"
    if abs(x) < 1e-3 or abs(x) >= 1e5:
        s = f"{x:.{n}e}"
        m, e = s.split("e")
        return rf"${m}\times 10^{{{int(e)}}}$"
    return f"{x:.{n}f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--report", default="report")
    args = ap.parse_args()

    cases = {}
    for cid in CASE_ORDER:
        d = os.path.join(args.results, cid)
        if not os.path.exists(os.path.join(d, "metadata.json")):
            continue
        cases[cid] = dict(
            dir=d,
            meta=json.load(open(os.path.join(d, "metadata.json"))),
            status=json.load(open(os.path.join(d, "run_status.json"))),
            forces=load_csv(os.path.join(d, "forces.csv")),
            residuals=load_csv(os.path.join(d, "residuals.csv")),
            surface=load_csv(os.path.join(d, "surface.csv")),
            part=(json.load(open(os.path.join(d, "partition_diagnostics.json")))
                  if os.path.exists(os.path.join(d, "partition_diagnostics.json")) else None),
        )

    tables = {}     # name -> list of lines
    M = []          # macros file
    T = []          # current table buffer

    def flush(name):
        nonlocal T
        tables[name] = list(T)
        T = []

    def macro(name, value):
        M.append(rf"\newcommand{{\{name}}}{{{value}}}")

    # ------------------------------------------------------------------ run table
    T.append(r"% ---- run status table")
    T.append(r"\begin{tabular}{lrrrrrl}")
    T.append(r"\toprule")
    T.append(r"case & ranks & steps & $t_{\mathrm{final}}$ & residual & wall time & status\\")
    T.append(r" & & & & orders & [s] & \\")
    T.append(r"\midrule")
    for cid, c in cases.items():
        s = c["status"]
        T.append(rf"{PRETTY[cid]} & {int(s['mpi_ranks'])} & {int(s['final_step'])} & "
                 rf"{float(s['final_physical_time']):.1f} & "
                 rf"{float(s['residual_reduction_orders']):.2f} & "
                 rf"{float(s['wall_time_seconds']):.0f} & "
                 rf"{STATUS_TEX[s['convergence_status']]}\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("runs")

    # ------------------------------------------------------------------ force table
    T.append(r"% ---- force coefficient table")
    T.append(r"\begin{tabular}{lrrrrrl}")
    T.append(r"\toprule")
    T.append(r"case & $C_L$ & $C_D$ & $C_{D,p}$ & $C_{D,f}$ & $C_{m,z}$ & status\\")
    T.append(r"\midrule")
    for cid, c in cases.items():
        f = c["forces"]
        transient = float(c["meta"].get("physical_time_step", 0.0)) > 0.0
        if transient:
            k = slice(int(0.5 * len(f["cd"])), None)
            cl, cd = np.mean(f["cl"][k]), np.mean(f["cd"][k])
            cdp, cdf = np.mean(f["pressure_drag"][k]), np.mean(f["viscous_drag"][k])
            cm = np.mean(f["cmz"][k])
            note = r"$^{\dagger}$"
        else:
            cl, cd = f["cl"][-1], f["cd"][-1]
            cdp, cdf, cm = f["pressure_drag"][-1], f["viscous_drag"][-1], f["cmz"][-1]
            note = ""
        T.append(rf"{PRETTY[cid]}{note} & {cl:+.5f} & {cd:.5f} & {cdp:.5f} & {cdf:.5f} & "
                 rf"{cm:+.5f} & {STATUS_TEX[c['status']['convergence_status']]}\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("forces")

    # ------------------------------------------------------------------ case table
    T.append(r"% ---- case physical parameters")
    T.append(r"\begin{tabular}{llrrrrl}")
    T.append(r"\toprule")
    T.append(r"case & mesh & $M_\infty$ & AoA & $Re$ & cells & wall BC\\")
    T.append(r"\midrule")
    for cid, c in cases.items():
        j = json.load(open(os.path.join(CASE_DIR, cid + ".json")))
        bc = [v for k, v in j["boundary_conditions"].items() if v != "farfield"]
        re_num = j["physics"].get("reynolds", 0.0)
        T.append(rf"{PRETTY[cid]} & {esc(os.path.basename(j['mesh']['file']))} & "
                 rf"{j['freestream']['mach']:g} & {j['freestream']['aoa_degrees']:g}$^\circ$ & "
                 rf"{('--' if not re_num else f'{re_num:g}')} & "
                 rf"{int(c['meta']['num_cells_global'])} & {esc(bc[0] if bc else '--')}\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("cases")

    # ------------------------------------------------------------------ numerics table
    T.append(r"% ---- production numerical settings actually used")
    T.append(r"\begin{tabular}{lrrrrrl}")
    T.append(r"\toprule")
    T.append(r"case & max steps & CFL & ramp & inner & target & extra options\\")
    T.append(r"\midrule")
    for cid, c in cases.items():
        m = c["meta"]
        cmd = m.get("command", "")
        extra = []
        for opt in ("--freeze-limiter-step", "--cfl-scale", "--inner-target", "--first-order"):
            mm = re.search(re.escape(opt) + r"(?:\s+(\S+))?", cmd)
            if mm:
                extra.append(esc(opt + (" " + mm.group(1) if mm.group(1) else "")))
        steps = int(c["status"]["final_step"])
        T.append(rf"{PRETTY[cid]} & {steps} & "
                 rf"{float(m['cfl_initial']):g}--{float(m['cfl_max']):g} & "
                 rf"{int(m['pseudo_cfl_ramp_steps'])} & "
                 rf"{int(m['min_inner_iterations'])}--{int(m['max_inner_iterations'])} & "
                 rf"{float(m['inner_residual_reduction_target']):g} & "
                 rf"{{\footnotesize {', '.join(extra) if extra else '--'}}}\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("numerics")

    # ------------------------------------------------------------------ inner iteration stats
    T.append(r"% ---- inner iteration statistics")
    T.append(r"\begin{tabular}{lrrrrrr}")
    T.append(r"\toprule")
    T.append(r"case & min & mean & max & target & misses & converged\\")
    T.append(r" & & & & & & fraction\\")
    T.append(r"\midrule")
    for cid, c in cases.items():
        m = c["meta"]
        T.append(rf"{PRETTY[cid]} & {int(m['observed_min_inner_iterations'])} & "
                 rf"{float(m['observed_mean_inner_iterations']):.2f} & "
                 rf"{int(m['observed_max_inner_iterations'])} & "
                 rf"{float(m['inner_residual_reduction_target']):g} & "
                 rf"{int(m['inner_target_misses'])} & "
                 rf"{100.0 * float(m['inner_target_converged_fraction']):.2f}\%\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("inner")

    # ------------------------------------------------------------------ partition table
    # Reference partition table: the production case with the most MPI ranks.
    ref = None
    for c in cases.values():
        if c["part"] is None:
            continue
        if ref is None or int(c["meta"]["mpi_ranks"]) > int(ref["meta"]["mpi_ranks"]):
            ref = c
    if ref is not None:
        macro("partRefCase", esc(ref["meta"]["case_id"]))
        macro("partRefRanks", str(int(ref["meta"]["mpi_ranks"])))
        p = ref["part"]
        T.append(r"% ---- per-rank partition diagnostics of the reference np=8 run")
        T.append(r"\begin{tabular}{rrrrrrr}")
        T.append(r"\toprule")
        T.append(r"rank & owned & ghost & boundary faces & neighbours & sent & received\\")
        T.append(r"\midrule")
        for r in p["ranks"]:
            T.append(rf"{r['rank']} & {r['num_cells_owned']} & {r['num_cells_ghost']} & "
                     rf"{r['num_boundary_faces']} & {r['num_neighbor_ranks']} & "
                     rf"{r['send_cells']} & {r['recv_cells']}\\")
        T.append(r"\midrule")
        T.append(rf"total & {p['total_owned_cells']} & "
                 rf"{sum(r['num_cells_ghost'] for r in p['ranks'])} & "
                 rf"{sum(r['num_boundary_faces'] for r in p['ranks'])} & & "
                 rf"{sum(r['send_cells'] for r in p['ranks'])} & "
                 rf"{sum(r['recv_cells'] for r in p['ranks'])}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("partition")
        macro("partEdgeCut", f"{p['edge_cut']}")
        macro("partLoadBalance", f"{p['load_balance_ratio']:.4f}")
        macro("partMinOwned", f"{p['min_owned_cells']}")
        macro("partMaxOwned", f"{p['max_owned_cells']}")

    # ------------------------------------------------------------------ MPI study
    mpi_csv = os.path.join(args.report, "mpi_study.csv")
    if os.path.exists(mpi_csv):
        rows = list(csv.DictReader(open(mpi_csv, newline="")))
        T.append(r"% ---- MPI rank-count study")
        T.append(r"\begin{tabular}{lrrrrrrrr}")
        T.append(r"\toprule")
        T.append(r"case & ranks & wall [s] & speed-up & $C_L$ & $C_D$ & "
                 r"$\Delta C_D/C_D$ & edge cut & balance\\")
        T.append(r"\midrule")
        last = None
        for r in rows:
            if last is not None and r["case_id"] != last:
                T.append(r"\midrule")
            last = r["case_id"]
            T.append(rf"{esc(r['case_id'])} & {r['mpi_ranks']} & {float(r['wall_time_s']):.1f} & "
                     rf"{float(r['speedup']):.2f} & {float(r['cl']):+.6f} & "
                     rf"{float(r['cd']):.6f} & {fmt(float(r.get('cd_rel_diff_vs_ref', r.get('cd_rel_diff_vs_np1', 0.0))), 2)} & "
                     rf"{r['edge_cut']} & {float(r['load_balance']):.4f}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("mpi")

    # ------------------------------------------------------------------ flux cross-check
    # Pairs each production (HLLC) run with the Roe run of the same case in
    # studies/verify, so the comparison table is generated from submitted data.
    flux_rows = []
    for cid, c in cases.items():
        alt = os.path.join("studies", "verify", cid + "_roe")
        if not os.path.exists(os.path.join(alt, "metadata.json")):
            continue
        am = json.load(open(os.path.join(alt, "metadata.json")))
        astatus = json.load(open(os.path.join(alt, "run_status.json")))
        flux_rows.append((PRETTY[cid], c["meta"], c["status"], am, astatus))
    if flux_rows:
        T.append(r"% ---- Roe vs HLLC cross-check")
        T.append(r"\begin{tabular}{llrrrr}")
        T.append(r"\toprule")
        T.append(r"case & flux & $C_L$ & $C_D$ & residual orders & steps\\")
        T.append(r"\midrule")
        for name, m1, s1, m2, s2 in flux_rows:
            T.append(rf"{name} & {esc(m1['inviscid_flux'])} & {float(m1['final_cl']):+.6f} & "
                     rf"{float(m1['final_cd']):.6f} & "
                     rf"{float(s1['residual_reduction_orders']):.2f} & {int(s1['final_step'])}\\")
            T.append(rf" & {esc(m2['inviscid_flux'])} + Harten--Yee & {float(m2['final_cl']):+.6f} & "
                     rf"{float(m2['final_cd']):.6f} & "
                     rf"{float(s2['residual_reduction_orders']):.2f} & {int(s2['final_step'])}\\")
            rel = abs(float(m1['final_cd']) - float(m2['final_cd'])) / max(abs(float(m1['final_cd'])), 1e-30)
            T.append(rf" & relative $C_D$ difference & & {fmt(rel, 2)} & & \\")
            T.append(r"\midrule")
        T[-1] = r"\bottomrule"
        T.append(r"\end{tabular}")
        flush("flux")

    # ------------------------------------------------------------------ dual-time CFL study
    cflroot = os.path.join("studies", "cflstudy")
    if os.path.isdir(cflroot):
        entries = []
        for d in sorted(os.listdir(cflroot)):
            mp = os.path.join(cflroot, d, "metadata.json")
            if not os.path.exists(mp):
                continue
            mm = json.load(open(mp))
            ss = json.load(open(os.path.join(cflroot, d, "run_status.json")))
            ff = load_csv(os.path.join(cflroot, d, "forces.csv"))
            entries.append((float(mm["effective_cfl_max"]),
                            float(mm["inner_residual_reduction_target"]),
                            float(mm["observed_mean_inner_iterations"]),
                            float(ss["wall_time_seconds"]), float(ff["cd"][-1]),
                            float(ss["final_physical_time"])))
        if entries:
            entries.sort(key=lambda e: (-e[1], e[0]))
            ref = min(entries, key=lambda e: e[1])[4]   # tightest inner target
            T.append(r"% ---- dual-time CFL / inner-target study")
            T.append(r"\begin{tabular}{rrrrrr}")
            T.append(r"\toprule")
            T.append(r"pseudo-CFL & inner target & mean inner its & wall time [s] & "
                     r"$C_D(t=" + f"{entries[0][5]:g}" + r")$ & $|\Delta C_D|$ vs.\ tightest\\")
            T.append(r"\midrule")
            for cfl, tgt, mean_it, wall, cdv, _t in entries:
                T.append(rf"{cfl:g} & {fmt(tgt,0)} & {mean_it:.1f} & {wall:.1f} & {cdv:.6f} & "
                         rf"{fmt(abs(cdv-ref),1)}\\")
            T.append(r"\bottomrule")
            T.append(r"\end{tabular}")
            flush("cflstudy")

    # ------------------------------------------------------------------ verification
    vpath = os.path.join(args.report, "verification.json")
    if os.path.exists(vpath):
        v = json.load(open(vpath))
        T.append(r"% ---- manufactured-solution order study")
        T.append(r"\begin{tabular}{lrrrrr}")
        T.append(r"\toprule")
        T.append(r"scheme & cells & mean $h$ & $L_1$ error & $L_2$ error & local order\\")
        T.append(r"\midrule")
        for key, label in (("mms_second_order_unlimited", "2nd order, unlimited"),
                           ("mms_second_order_limited", "2nd order, Venkatakrishnan"),
                           ("mms_first_order", "1st order")):
            lv = v.get(key, [])
            for i, l in enumerate(lv):
                order = ""
                if i > 0 and l["err_l1"] > 0 and lv[i - 1]["err_l1"] > 0:
                    order = (f"{np.log(lv[i-1]['err_l1'] / l['err_l1']) / np.log(lv[i-1]['mean_h'] / l['mean_h']):.2f}")
                name = label if i == 0 else ""
                T.append(rf"{name} & {l['num_cells']} & {l['mean_h']:.4f} & "
                         rf"{fmt(l['err_l1'], 3)} & {fmt(l['err_l2'], 3)} & {order}\\")
            T.append(r"\midrule")
        T[-1] = r"\bottomrule"
        T.append(r"\end{tabular}")
        flush("mms")
        macro("mmsOrderSecond", f"{v['mms_observed_order_second_order_unlimited']:.2f}")
        macro("mmsOrderSecondLimited", f"{v['mms_observed_order_second_order_limited']:.2f}")
        macro("mmsOrderFirst", f"{v['mms_observed_order_first_order']:.2f}")
        macro("truncOrderSecond", f"{v['observed_order_second_order_l1']:.2f}")
        macro("truncOrderFirst", f"{v['observed_order_first_order_l1']:.2f}")
        if "freestream_residual_linf" in v:
            macro("freestreamResidual", fmt(v["freestream_residual_linf"], 2))
            macro("linearGradientError", fmt(v["linear_gradient_max_error"], 2))
            macro("linearReconError", fmt(v["linear_reconstruction_max_error"], 2))

    # ------------------------------------------------------------------ Strouhal
    spath = os.path.join(args.report, "shedding_analysis.json")
    if os.path.exists(spath):
        sa = json.load(open(spath))
        macro("stFft", f"{sa['strouhal_fft']:.4f}")
        macro("stZc", f"{sa['strouhal_zero_crossing']:.4f}"
              if sa.get("strouhal_zero_crossing") else "n/a")
        macro("meanCdReTwoHundred", f"{sa['mean_cd']:.4f}")
        macro("clAmpReTwoHundred", f"{sa['cl_amplitude']:.4f}")
        macro("clRmsReTwoHundred", f"{sa['cl_rms']:.4f}")
        macro("cdAmpReTwoHundred", f"{sa['cd_amplitude']:.4f}")
        macro("sheddingPeriods", f"{sa['num_periods_detected']}")
        macro("meanPressureDragReTwoHundred", f"{sa['mean_pressure_drag']:.4f}")
        macro("meanViscousDragReTwoHundred", f"{sa['mean_viscous_drag']:.4f}")

    # ------------------------------------------------------------------ scalar macros
    if cases:
        any_naca = next((c for k, c in cases.items() if k.startswith("naca")), None)
        any_cyl = next((c for k, c in cases.items() if k.startswith("cyl")), None)
        if any_naca:
            macro("nacaCells", f"{int(any_naca['meta']['num_cells_global'])}")
            macro("nacaFaces", f"{int(any_naca['meta']['num_faces_global'])}")
            macro("nacaNodes", f"{int(any_naca['meta']['mesh_nodes_global'])}")
            macro("nacaBFaces", f"{int(any_naca['meta']['num_boundary_faces_global'])}")
        if any_cyl:
            macro("cylCells", f"{int(any_cyl['meta']['num_cells_global'])}")
            macro("cylFaces", f"{int(any_cyl['meta']['num_faces_global'])}")
            macro("cylNodes", f"{int(any_cyl['meta']['mesh_nodes_global'])}")
            macro("cylBFaces", f"{int(any_cyl['meta']['num_boundary_faces_global'])}")
            macro("cylZones", f"{int(any_cyl['meta']['mesh_zones'])}")
        digits = {"0": "Zero", "1": "One", "2": "Two", "3": "Three", "4": "Four",
                  "5": "Five", "6": "Six", "7": "Seven", "8": "Eight", "9": "Nine"}
        for cid, c in cases.items():
            key = "".join(w.capitalize() for w in cid.split("_"))
            key = "".join(digits.get(ch, ch) for ch in key)   # TeX macros cannot contain digits
            transient = float(c["meta"].get("physical_time_step", 0.0)) > 0.0
            if transient:   # quote the same time average that tab:forces shows
                k = slice(int(0.5 * len(c["forces"]["cd"])), None)
                cd_q, cl_q = float(np.mean(c["forces"]["cd"][k])), float(np.mean(c["forces"]["cl"][k]))
            else:
                cd_q, cl_q = float(c["forces"]["cd"][-1]), float(c["forces"]["cl"][-1])
            macro("cd" + key, f"{cd_q:.5f}")
            macro("cl" + key, f"{cl_q:+.5f}")
            macro("orders" + key, f"{float(c['status']['residual_reduction_orders']):.2f}")
            macro("wall" + key, f"{float(c['status']['wall_time_seconds']):.0f}")
        macro("numCases", f"{len(cases)}")
        nfail = sum(1 for c in cases.values()
                    if c["status"]["convergence_status"] == "failed")
        macro("numFailedCases", f"{nfail}")

    # Sanity-check summary
    sp = os.path.join(args.report, "sanity_checks.json")
    if os.path.exists(sp):
        sc = json.load(open(sp))
        macro("sanityFailures", f"{sc['num_failed_checks']}")
        macro("sanityCases", f"{sc['num_cases']}")
        total = sum(len(c["checks"]) for c in sc["cases"])
        macro("sanityTotalChecks", f"{total}")

    tdir = os.path.join(args.report, "tables")
    os.makedirs(tdir, exist_ok=True)
    for name, lines in tables.items():
        open(os.path.join(tdir, f"tab_{name}.tex"), "w").write("\n".join(lines) + "\n")
    # Placeholder for any table whose source data is not present yet, so the
    # report always compiles.
    bad = [l for l in M if not re.match(r"^\\newcommand\{\\[A-Za-z]+\}", l)]
    if bad:
        raise SystemExit("generated macro names must be purely alphabetic: " + str(bad[:3]))
    for name in ("runs", "cases", "forces", "numerics", "inner", "partition", "mpi", "mms",
                 "flux", "cflstudy"):
        p = os.path.join(tdir, f"tab_{name}.tex")
        if not os.path.exists(p):
            open(p, "w").write(r"\emph{(data not available)}" + "\n")
    # Any macro the report uses but this run could not populate gets a visible
    # "??" fallback, so a missing data file shows up in the PDF instead of
    # breaking the build.
    tex_path = os.path.join(args.report, "report.tex")
    if os.path.exists(tex_path):
        defined = {re.match(r"\\newcommand\{\\([A-Za-z]+)\}", l).group(1) for l in M}
        body = open(tex_path).read()
        used = set(re.findall(r"\\([a-zA-Z]+)", body))
        candidates = sorted(m for m in used
                            if m not in defined and m[0].islower()
                            and any(c.isupper() for c in m) and len(m) > 3
                            and m not in LATEX_BUILTINS)
        if candidates:
            M.append("% fallbacks for macros this run could not populate")
            M += [rf"\providecommand{{\{m}}}{{\textbf{{??}}}}" for m in candidates]
            print(f"  {len(candidates)} macro(s) not populated: {', '.join(candidates[:6])}"
                  + (" ..." if len(candidates) > 6 else ""))
    open(os.path.join(args.report, "generated_macros.tex"), "w").write("\n".join(M) + "\n")
    print(f"wrote {tdir}/tab_*.tex ({len(tables)} tables) and "
          f"generated_macros.tex ({len(M)} macros)")


if __name__ == "__main__":
    main()

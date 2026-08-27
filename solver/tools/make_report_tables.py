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
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vtu_reader import read_vtu  # noqa: E402

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


def sci(x, n=2):
    """Always-scientific formatter, for quantities that span many decades."""
    if x is None:
        return "--"
    if x == 0:
        return "0"
    s = f"{x:.{n}e}"
    m, e = s.split("e")
    return rf"${m}\times 10^{{{int(e)}}}$"


def fmt(x, n=4):
    """Fixed-point when that shows at least two significant digits, else
    scientific.  The switch is tied to the requested number of decimals so a
    value like 2.4e-3 printed with two decimals does not come out as 0.00."""
    if x is None:
        return "--"
    if isinstance(x, str):
        return esc(x)
    if x == 0:
        return "0"
    if abs(x) < 10.0 ** (-(n - 1)) or abs(x) >= 1e5:
        return sci(x, max(n, 2))
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
        # Size every generated table by its column count: these are wide
        # numerical tables and the default 10 pt overruns the text block.
        nonlocal T
        body = list(T)
        if not any(l.startswith(r"\small") or l.startswith(r"\footnotesize") for l in body):
            ncol = 0
            for l in body:
                m = re.match(r"\\begin\{tabular\}\{([^}]*)\}", l)
                if m:
                    ncol = sum(1 for ch in m.group(1) if ch in "lrc")
                    break
            size = r"\footnotesize" if ncol >= 6 else r"\small"
            insert = next((i for i, l in enumerate(body) if l.startswith(r"\begin{tabular}")), 0)
            body.insert(insert, size)
        tables[name] = body
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
        mesh_name = os.path.basename(j["mesh"]["file"]).replace(".cgns", "")
        wall = {"no_slip_adiabatic_wall": "no-slip adiabatic",
                "slip_wall": "slip"}.get(bc[0] if bc else "", bc[0] if bc else "--")
        T.append(rf"{PRETTY[cid]} & {esc(mesh_name)} & "
                 rf"{j['freestream']['mach']:g} & {j['freestream']['aoa_degrees']:g}$^\circ$ & "
                 rf"{('--' if not re_num else f'{re_num:g}')} & "
                 rf"{int(c['meta']['num_cells_global'])} & {esc(wall)}\\")
    T.append(r"\bottomrule")
    T.append(r"\end{tabular}")
    flush("cases")

    # ------------------------------------------------------------------ numerics table
    T.append(r"% ---- production numerical settings actually used")
    T.append(r"\begin{tabular}{lrrrrrl}")
    T.append(r"\toprule")
    T.append(r"case & steps used & CFL & ramp & inner & target & extra options\\")
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
        T.append(r"\begin{tabular}{lrrrrrrrrr}")
        T.append(r"\toprule")
        T.append(r"case & ranks & steps & wall [s] & speed-up & $C_L$ & $C_D$ & "
                 r"$\Delta C_D/C_D$ & edge cut & balance\\")
        T.append(r"\midrule")
        last = None
        for r in rows:
            if last is not None and r["case_id"] != last:
                T.append(r"\midrule")
            last = r["case_id"]
            T.append(rf"{esc(r['case_id'])} & {r['mpi_ranks']} & {int(float(r['steps']))} & "
                     rf"{float(r['wall_time_s']):.1f} & "
                     rf"{float(r['speedup']):.2f} & {float(r['cl']):+.6f} & "
                     rf"{float(r['cd']):.6f} & "
                     rf"{sci(float(r.get('cd_rel_diff_vs_ref', 0.0)), 2)} & "
                     rf"{r['edge_cut']} & {float(r['load_balance']):.4f}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("mpi")
        # Consistency and balance figures quoted in the prose, so that the text
        # cannot drift away from the table above.
        per_case = {}
        for r in rows:
            per_case.setdefault(r["case_id"], []).append(r)
        for cid, rr in per_case.items():
            tag = "Cyl" if "cylinder" in cid else "Naca"
            rel = max(abs(float(x.get("cd_rel_diff_vs_ref", 0.0))) for x in rr)
            absd = max(abs(float(x["cd"]) - float(rr[0]["cd"])) for x in rr)
            macro("mpiMaxRelCd" + tag, sci(rel, 1))
            macro("mpiMaxAbsCd" + tag, sci(absd, 1))
            macro("mpiCd" + tag, f"{float(rr[0]['cd']):.4f}")
            for nr, word in ((2, "Two"), (4, "Four"), (8, "Eight")):
                sp = [float(x["speedup"]) for x in rr if int(x["mpi_ranks"]) == nr]
                if sp:
                    macro("mpiSpeedup" + word + tag, f"{sp[0]:.2f}")
                    if nr == 4:
                        macro("mpiEfficiencyFour" + tag, f"{100.0 * sp[0] / 4.0:.0f}")
        macro("mpiMaxImbalance",
              f"{100.0 * (max(float(r['load_balance']) for r in rows) - 1.0):.1f}")

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
        T.append(r"\begin{tabular}{llrrrrl}")
        T.append(r"\toprule")
        T.append(r"case & flux & $C_L$ & $C_D$ & residual orders & steps & status\\")
        T.append(r"\midrule")
        for name, m1, s1, m2, s2 in flux_rows:
            T.append(rf"{name} & {esc(m1['inviscid_flux'])} & {float(m1['final_cl']):+.6f} & "
                     rf"{float(m1['final_cd']):.6f} & "
                     rf"{float(s1['residual_reduction_orders']):.2f} & {int(s1['final_step'])} & "
                     rf"{esc(s1['convergence_status'])}\\")
            T.append(rf" & {esc(m2['inviscid_flux'])} + Harten--Yee & {float(m2['final_cl']):+.6f} & "
                     rf"{float(m2['final_cd']):.6f} & "
                     rf"{float(s2['residual_reduction_orders']):.2f} & {int(s2['final_step'])} & "
                     rf"{esc(s2['convergence_status'])}\\")
            rel = abs(float(m1['final_cd']) - float(m2['final_cd'])) / max(abs(float(m1['final_cd'])), 1e-30)
            T.append(rf" & relative $C_D$ difference & & {sci(rel, 2)} & & & \\")
            T.append(r"\midrule")
        T[-1] = r"\bottomrule"
        T.append(r"\end{tabular}")
        flush("flux")

    # ------------------------------------------------------------------ laminar aerofoil diagnostics
    for cid, tag in (("naca0012_m015_laminar_re5000", "LamMZeroOneFive"),
                     ("naca0012_m080_laminar_re5000", "LamMZeroEightZero"),
                     ("naca0012_m200_laminar_re5000", "LamMTwoZeroZero"),
                     ("naca0012_m015_inviscid", "InvMZeroOneFive"),
                     ("naca0012_m080_inviscid", "InvMZeroEightZero"),
                     ("naca0012_m200_inviscid", "InvMTwoZeroZero")):
        c = cases.get(cid)
        if c is None:
            continue
        sf = c["surface"]
        x, cpv, cfv, ny = sf["x"], sf["cp"], sf["cf"], sf["ny"]
        chord = float(x.max() - x.min())
        xc = (x - x.min()) / chord
        up = ny < 0.0
        o = np.argsort(xc[up])
        xu, cfu = xc[up][o], cfv[up][o]
        # skin friction at 30% chord, and the last upper-surface Cf sign change
        i30 = int(np.argmin(np.abs(xu - 0.3)))
        macro("cfThirtyChord" + tag, f"{cfu[i30]:.4f}")
        z = np.where(np.diff(np.sign(cfu)) != 0)[0]
        inner = [xu[i] for i in z if xu[i] < 0.99]
        macro("separationX" + tag, f"{inner[0]:.2f}" if inner else "none")
        macro("cpMin" + tag, f"{cpv.min():.3f}")
        macro("cdp" + tag, f"{float(c['meta']['final_pressure_drag']):.4f}")
        macro("cdf" + tag, f"{float(c['meta']['final_viscous_drag']):.4f}")
        try:
            mesh = read_vtu(os.path.join(c["dir"], "field_final.vtu"))
            macro("machMax" + tag, f"{mesh.cell_data['Mach'].max():.2f}")
            macro("supersonicCells" + tag, f"{int((mesh.cell_data['Mach'] > 1.0).sum())}")
        except Exception:
            pass
    macro("blasiusCfThirty", f"{0.664 / np.sqrt(5000 * 0.3):.4f}")

    # ------------------------------------------------------------------ Re20 validation
    c20 = cases.get("cylinder_m010_laminar_re20")
    if c20 is not None:
        sf = c20["surface"]
        x, y = sf["x"], sf["y"]
        cp20, cf20 = sf["cp"], sf["cf"]
        th = np.degrees(np.arctan2(y, x)) % 360.0
        o = np.argsort(th)
        ths, cfs = th[o], cf20[o]
        zeros = [ths[i] for i in np.where(np.diff(np.sign(cfs)) != 0)[0]]
        # separation measured from the rear stagnation line (theta = 0)
        sep = sorted(min(z, 360.0 - z) for z in zeros)
        i_front = int(np.argmin(np.abs(th - 180.0)))
        i_rear = int(np.argmin(np.abs(th - 0.0)))
        i_min = int(np.argmin(cp20))
        m20 = c20["meta"]
        T.append(r"% ---- Re 20 cylinder against literature")
        T.append(r"\begin{tabular}{lrl}")
        T.append(r"\toprule")
        T.append(r"quantity & computed & accepted\\")
        T.append(r"\midrule")
        rows20 = [
            (r"total drag $C_D$", f"{float(m20['final_cd']):.4f}", r"$2.0$--$2.05$"),
            (r"pressure drag $C_{D,p}$", f"{float(m20['final_pressure_drag']):.4f}",
             r"$\approx1.23$"),
            (r"skin-friction drag $C_{D,f}$", f"{float(m20['final_viscous_drag']):.4f}",
             r"$\approx0.81$"),
            (r"lift $C_L$ (symmetry)", f"{float(m20['final_cl']):+.6f}", r"$0$"),
            (r"front stagnation $C_p$", f"{cp20[i_front]:.4f}", r"$\approx1.26$"),
            (r"base pressure $C_p$ at $\theta=0^\circ$", f"{cp20[i_rear]:.4f}",
             r"$\approx-0.55$"),
            (r"minimum $C_p$", f"{cp20[i_min]:.4f} at "
             rf"${abs(180.0 - th[i_min]):.0f}^\circ$ from stagnation", r"--"),
            (r"separation angle from the rear",
             (rf"${sep[0]:.0f}^\circ$--${sep[-1]:.0f}^\circ$" if sep else "--"),
             r"$43^\circ$--$45^\circ$"),
        ]
        for a, b, c in rows20:
            T.append(rf"{a} & {b} & {c}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("cyl20")

    # ------------------------------------------------------------------ Re200 validation
    spath0 = os.path.join(args.report, "shedding_analysis.json")
    if os.path.exists(spath0):
        sa0 = json.load(open(spath0))
        T.append(r"% ---- Re 200 vortex street against literature")
        T.append(r"\begin{tabular}{lrl}")
        T.append(r"\toprule")
        T.append(r"quantity & computed & accepted\\")
        T.append(r"\midrule")
        rows = [
            (r"Strouhal number $St=fD/U_\infty$ (spectral peak)",
             f"{sa0['strouhal_fft']:.4f}", r"$0.19$--$0.20$"),
            (r"Strouhal number (lift up-crossings)",
             (f"{sa0['strouhal_zero_crossing']:.4f}"
              if sa0.get("strouhal_zero_crossing") else "--"), r"$0.19$--$0.20$"),
            (r"mean drag $\overline{C_D}$", f"{sa0['mean_cd']:.4f}", r"$1.32$--$1.40$"),
            (r"\quad pressure part", f"{sa0['mean_pressure_drag']:.4f}", r"--"),
            (r"\quad skin-friction part", f"{sa0['mean_viscous_drag']:.4f}", r"--"),
            (r"lift amplitude", f"{sa0['cl_amplitude']:.4f}", r"$0.60$--$0.69$"),
            (r"lift RMS", f"{sa0['cl_rms']:.4f}", r"$0.42$--$0.49$"),
            (r"drag oscillation amplitude", f"{sa0['cd_amplitude']:.4f}", r"--"),
            (r"mean lift (symmetry)", f"{sa0['mean_cl']:+.5f}", r"$0$"),
            (r"shedding cycles analysed", f"{sa0['num_periods_detected']}", r"--"),
        ]
        for a, b, c in rows:
            T.append(rf"{a} & {b} & {c}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("re200")

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
                T.append(rf"{cfl:g} & {sci(tgt,0)} & {mean_it:.1f} & {wall:.1f} & {cdv:.6f} & "
                         rf"{sci(abs(cdv-ref),2)}\\")
            T.append(r"\bottomrule")
            T.append(r"\end{tabular}")
            flush("cflstudy")
            byname = {}
            for d in sorted(os.listdir(cflroot)):
                mp = os.path.join(cflroot, d, "metadata.json")
                if os.path.exists(mp):
                    byname[d] = (json.load(open(mp)),
                                 json.load(open(os.path.join(cflroot, d, "run_status.json"))),
                                 load_csv(os.path.join(cflroot, d, "forces.csv")))
            ref_name = "cfl30_t1em5"
            if ref_name in byname:
                cd_ref = float(byname[ref_name][2]["cd"][-1])
                def dev(name):
                    return abs(float(byname[name][2]["cd"][-1]) - cd_ref) / abs(cd_ref)
                if "cfl1_t1em3" in byname and "cfl30_t1em4" in byname:
                    macro("cflSuppliedDeviation", f"{100 * dev('cfl1_t1em3'):.2f}")
                    macro("cflProductionDeviation", f"{100 * dev('cfl30_t1em4'):.3f}")
                    macro("cflSuppliedWall", f"{byname['cfl1_t1em3'][1]['wall_time_seconds']:.1f}")
                    macro("cflProductionWall",
                          f"{byname['cfl30_t1em4'][1]['wall_time_seconds']:.1f}")
                    macro("cflSuppliedInner",
                          f"{byname['cfl1_t1em3'][0]['observed_mean_inner_iterations']:.0f}")
                    macro("cflProductionInner",
                          f"{byname['cfl30_t1em4'][0]['observed_mean_inner_iterations']:.0f}")
                if "cfl100_t1em3" in byname:
                    a = float(byname["cfl1_t1em3"][2]["cd"][-1])
                    b = float(byname["cfl100_t1em3"][2]["cd"][-1])
                    macro("cflSpreadAtLooseTarget", f"{100 * abs(a - b) / abs(a):.2f}")
                if "cfl100_t1em4" in byname and "cfl30_t1em4" in byname:
                    a = float(byname["cfl30_t1em4"][2]["cd"][-1])
                    b = float(byname["cfl100_t1em4"][2]["cd"][-1])
                    macro("cflSpreadAtTightTarget", sci(abs(a - b) / abs(a), 1))

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
                         rf"{sci(l['err_l1'], 3)} & {sci(l['err_l2'], 3)} & {order}\\")
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
            macro("freestreamResidual", sci(v["freestream_residual_linf"], 2))
            macro("linearGradientError", sci(v["linear_gradient_max_error"], 2))
            macro("linearReconError", sci(v["linear_reconstruction_max_error"], 2))

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

    # First-order reference run: shows what the linear reconstruction buys.
    o1 = os.path.join("studies", "verify", "naca0012_m015_laminar_re5000_o1")
    base = cases.get("naca0012_m015_laminar_re5000")
    if base is not None and os.path.exists(os.path.join(o1, "metadata.json")):
        m1 = json.load(open(os.path.join(o1, "metadata.json")))
        macro("cdFirstOrderLaminar", f"{float(m1['final_cd']):.5f}")
        macro("cdfFirstOrderLaminar", f"{float(m1['final_viscous_drag']):.5f}")
        macro("cdpFirstOrderLaminar", f"{float(m1['final_pressure_drag']):.5f}")
        rel = (float(m1["final_cd"]) - float(base["meta"]["final_cd"])) / \
            float(base["meta"]["final_cd"])
        macro("cdFirstOrderExcess", f"{100.0 * rel:.0f}")

    # Re 200 dual-time settings cross-check at the same physical time.
    a = os.path.join("studies", "verify", "cylinder_m010_laminar_re200_suppliedcfl")
    b = os.path.join("studies", "verify", "cylinder_m010_laminar_re200_prodcfl20")
    if os.path.exists(os.path.join(a, "metadata.json")) and \
            os.path.exists(os.path.join(b, "metadata.json")):
        ma = json.load(open(os.path.join(a, "metadata.json")))
        mb = json.load(open(os.path.join(b, "metadata.json")))
        sa = json.load(open(os.path.join(a, "run_status.json")))
        sb = json.load(open(os.path.join(b, "run_status.json")))
        macro("cdReTwoHundredSupplied", f"{float(ma['final_cd']):.6f}")
        macro("cdReTwoHundredProduction", f"{float(mb['final_cd']):.6f}")
        macro("cdReTwoHundredSettingsDiff",
              fmt(abs(float(ma["final_cd"]) - float(mb["final_cd"])) /
                  abs(float(ma["final_cd"])), 1))
        macro("wallReTwoHundredSupplied", f"{float(sa['wall_time_seconds']):.0f}")
        macro("wallReTwoHundredProduction", f"{float(sb['wall_time_seconds']):.0f}")
        macro("innerReTwoHundredSupplied",
              f"{float(ma['observed_mean_inner_iterations']):.0f}")
        macro("innerReTwoHundredProduction",
              f"{float(mb['observed_mean_inner_iterations']):.0f}")
        macro("tReTwoHundredCrosscheck", f"{float(sa['final_physical_time']):g}")

    # Restart round trip.
    rc = os.path.join("studies", "restart", "restart_check.json")
    if os.path.exists(rc):
        r = json.load(open(rc))
        worst = max(v["abs_difference"] for k, v in r.items() if isinstance(v, dict))
        macro("restartMaxDifference", sci(worst, 1) if worst > 0 else "exactly zero")
        macro("restartPassed", "yes" if r.get("passed") else "no")

    # ------------------------------------------------- limiter freeze comparison
    # Every number the limitations section quotes about the residual limit cycle
    # is derived here, so the prose cannot drift away from the runs.
    FREEZE_PAIRS = [("naca0012_m080_inviscid", "MachZeroEight"),
                    ("naca0012_m200_inviscid", "MachTwo")]
    for cid, tag in FREEZE_PAIRS:
        nf = os.path.join("studies", "verify", cid + "_nofreeze")
        if cid not in cases or not os.path.exists(os.path.join(nf, "forces.csv")):
            continue
        cd_frozen = float(cases[cid]["forces"]["cd"][-1])
        f_nf = load_csv(os.path.join(nf, "forces.csv"))
        r_nf = load_csv(os.path.join(nf, "residuals.csv"))
        cd_free = float(f_nf["cd"][-1])
        macro("cdFrozen" + tag, f"{cd_frozen:.4g}")
        macro("cdUnfrozen" + tag, f"{cd_free:.4g}")
        macro("cdFreezeDiffPct" + tag,
              f"{100.0 * abs(cd_free - cd_frozen) / max(abs(cd_frozen), 1e-30):.2f}")
        rl = r_nf["residual_l2"]
        macro("residualStallUnfrozen" + tag, sci(float(rl[-1]) / float(rl[0]), 1))
        macro("stepsUnfrozen" + tag, f"{int(float(r_nf['step'][-1]))}")
        # Scatter of the drag over the acceptance window of the production run.
        cdp = cases[cid]["forces"]["cd"]
        w = max(50, len(cdp) // 10)
        seg = cdp[-w:]
        macro("cdScatterPct" + tag,
              f"{100.0 * float(np.std(seg)) / max(abs(float(np.mean(seg))), 1e-30):.2f}")
        prev = cdp[-2 * w:-w] if len(cdp) >= 2 * w else seg
        macro("cdDriftPct" + tag,
              f"{100.0 * abs(float(np.mean(seg)) - float(np.mean(prev))) / max(abs(float(np.mean(seg))), 1e-30):.2f}")
        macro("freezeStep" + tag,
              f"{int(cases[cid]['meta'].get('limiter_freeze_step', 0))}")

    # --------------------------------------------------------- unit tests
    # Parsed from the stored doctest summary so the report cannot claim a
    # different number of tests than the ones that actually ran.
    ut = os.path.join(args.report, "unit_tests.log")
    if os.path.exists(ut):
        txt = open(ut).read()
        m1 = re.search(r"test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed", txt)
        m2 = re.search(r"assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed", txt)
        if m1:
            macro("unitTestCases", m1.group(1))
            macro("unitTestFailures", m1.group(3))
        if m2:
            macro("unitTestAssertions", m2.group(1))
            macro("unitAssertionFailures", m2.group(3))

    # ------------------------------------------------------- mesh and flow facts
    mf = os.path.join(args.report, "mesh_facts.json")
    if os.path.exists(mf):
        F = json.load(open(mf))
        nm = F["meshes"].get("naca")
        cm = F["meshes"].get("cylinder")
        if nm:
            macro("nacaMaxAspectRatio", f"{nm['max_aspect_ratio']:.0f}")
            macro("nacaMedianCellArea", sci(nm["median_cell_area"], 1))
            macro("nacaMinCellArea", sci(nm["min_cell_area"], 1))
            ms = nm.get("leading_edge_mirror_symmetry")
            if ms:
                macro("meshAsymUpperCells", f"{ms['num_upper']}")
                macro("meshAsymLowerCells", f"{ms['num_lower']}")
                macro("meshAsymFractionPct",
                      f"{100.0 * ms['fraction_without_close_mirror']:.0f}")
            sl = nm.get("sliver_residual_concentration")
            if sl:
                macro("sliverCells", f"{sl['num_cells']}")
                macro("sliverMeanArea", sci(sl["mean_cell_area"], 1))
                macro("sliverAreaFraction", sci(sl["area_fraction"], 1))
        if cm:
            macro("cylinderMaxAspectRatio", f"{cm['max_aspect_ratio']:.0f}")
        li = F.get("aerofoil_final_residual_linf")
        if li:
            macro("linfPlateauMin", f"{li['min']:.0f}")
            macro("linfPlateauMax", f"{li['max']:.0f}")
        bl = F["cases"].get("naca0012_m200_laminar_re5000", {}).get("boundary_layer")
        if bl:
            macro("blThicknessMachTwo", f"{bl['delta99']:.3f}")
            macro("blOverHalfThickness", f"{bl['delta99_over_half_thickness']:.2f}")
        bz = F["cases"].get("naca0012_m015_laminar_re5000", {}).get("blasius")
        if bz:
            macro("blasiusComputedCf", f"{bz['computed_cf']:.5f}")
            macro("blasiusReferenceCf", f"{bz['blasius_cf']:.5f}")
            macro("blasiusDifferencePct", f"{100.0 * bz['relative_difference']:.1f}")

    # ------------------------------------------------ multidimensional shock fix
    # Mach 2 aerofoil with the fix on (production), off, and with Rusanov, which
    # is immune to the carbuncle because it does not resolve the contact wave.
    # The Rayleigh-Pitot ceiling is the largest stagnation pressure a normal
    # shock at this Mach number can produce, so a wall C_p above it is a
    # discretisation artefact by definition.
    def surface_facts(d):
        sp = os.path.join(d, "surface.csv")
        if not os.path.exists(sp):
            return None
        rows = list(csv.DictReader(open(sp, newline="")))
        x = np.array([float(r["x"]) for r in rows])
        cp = np.array([float(r["cp"]) for r in rows])
        ny = np.array([float(r["ny"]) for r in rows])
        up = ny < 0.0
        xu, cu = x[up], cp[up]
        xl, cl = x[~up], cp[~up]
        o = np.argsort(xu); xu, cu = xu[o], cu[o]
        o = np.argsort(xl); xl, cl = xl[o], cl[o]
        dif = np.abs(cu - np.interp(xu, xl, cl))
        return dict(cp_max=float(cp.max()),
                    sym_rms=float(np.sqrt((dif**2).mean())),
                    sym_max=float(dif.max()))

    def pitot_cp(mach, gamma=1.4):
        m2 = mach * mach
        ratio = (((gamma + 1) ** 2 * m2 / (4 * gamma * m2 - 2 * (gamma - 1)))
                 ** (gamma / (gamma - 1)) * (1 - gamma + 2 * gamma * m2) / (gamma + 1))
        return (ratio - 1.0) / (0.5 * gamma * m2)

    SF = [("HLLC + shock fix (production)", os.path.join(args.results, "naca0012_m200_inviscid")),
          ("HLLC, fix disabled", os.path.join("studies", "verify",
                                              "naca0012_m200_inviscid_noshockfix")),
          ("Rusanov", os.path.join("studies", "verify", "naca0012_m200_inviscid_rusanov"))]
    sf_rows = [(lbl, d, surface_facts(d)) for lbl, d in SF]
    sf_rows = [(l, d, f) for l, d, f in sf_rows
               if f is not None and os.path.exists(os.path.join(d, "metadata.json"))]
    if sf_rows:
        ceiling = pitot_cp(2.0)
        macro("pitotCeilingMachTwo", f"{ceiling:.3f}")
        T.append(r"% ---- multidimensional shock fix, Mach 2 aerofoil")
        T.append(r"\begin{tabular}{lrrrrr}")
        T.append(r"\toprule")
        T.append(r"variant & $C_D$ & $|C_L|$ & $C_p^{\max}$ & upper/lower $C_p$ rms & steps\\")
        T.append(r"\midrule")
        for lbl, d, f in sf_rows:
            m = json.load(open(os.path.join(d, "metadata.json")))
            st = json.load(open(os.path.join(d, "run_status.json")))
            T.append(rf"{esc(lbl)} & {float(m['final_cd']):.5f} & "
                     rf"{sci(abs(float(m['final_cl'])), 1)} & {f['cp_max']:.3f} & "
                     rf"{sci(f['sym_rms'], 1)} & {int(st['final_step'])}\\")
        T.append(rf"\midrule\multicolumn{{3}}{{l}}{{\emph{{Rayleigh--Pitot ceiling}}}} & "
                 rf"{ceiling:.3f} & \multicolumn{{2}}{{l}}{{\emph{{exact upper bound}}}}\\")
        T.append(r"\bottomrule")
        T.append(r"\end{tabular}")
        flush("shockfix")
        for lbl, key in (("HLLC + shock fix (production)", "Fixed"),
                         ("HLLC, fix disabled", "Unfixed"), ("Rusanov", "Rusanov")):
            for l, d, f in sf_rows:
                if l != lbl:
                    continue
                m = json.load(open(os.path.join(d, "metadata.json")))
                cd_ = float(m["final_cd"])
                cl_ = abs(float(m["final_cl"]))
                macro("shockFixCd" + key, f"{cd_:.5f}")
                macro("shockFixCl" + key, sci(cl_, 1))
                macro("shockFixClPercentOfDrag" + key, f"{100.0 * cl_ / abs(cd_):.1f}")
                macro("shockFixCpMax" + key, f"{f['cp_max']:.3f}")
                macro("shockFixCpExcess" + key,
                      f"{100.0 * (f['cp_max'] - ceiling) / ceiling:.0f}")
                macro("shockFixSym" + key, sci(f["sym_rms"], 1))

    # Cases where the sensor must be inactive: the run with --shock-fix 0 has to
    # reproduce the production run exactly.
    inv = []
    for cid, tag in (("cylinder_m010_laminar_re20", "Cylinder"),
                     ("naca0012_m015_laminar_re5000", "SubsonicLaminar"),
                     ("naca0012_m080_inviscid", "Transonic")):
        d0 = os.path.join("studies", "verify", cid + "_noshockfix")
        if cid in cases and os.path.exists(os.path.join(d0, "metadata.json")):
            a = float(cases[cid]["meta"]["final_cd"])
            b = float(json.load(open(os.path.join(d0, "metadata.json")))["final_cd"])
            rel = abs(a - b) / max(abs(a), 1e-30)
            macro("shockFixDiff" + tag, sci(rel, 1) if rel > 0 else "exactly zero")
            if tag != "Transonic":
                inv.append(rel)
    if inv:
        macro("shockFixInactiveMaxDiff", sci(max(inv), 1))
        macro("shockFixInactiveCases", str(len(inv)))

    # ------------------------------------------- Roe Mach 2 failure diagnostics
    roe2 = os.path.join("studies", "verify", "naca0012_m200_inviscid_roe", "metadata.json")
    if os.path.exists(roe2):
        rm = json.load(open(roe2))
        macro("roePositivityFallbacks",
              f"{int(rm.get('positivity_fallback_face_states', 0)):,}".replace(",", r"\,"))
        macro("roeOrdersMachTwo",
              f"{float(json.load(open(os.path.join(os.path.dirname(roe2), 'run_status.json')))['residual_reduction_orders']):.2f}")

    # ------------------------------------------------------------ code size
    # Counted here rather than typed, so the figure cannot go stale.
    nlines = 0
    for root, _dirs, files in os.walk("src"):
        for fn in files:
            if fn.endswith((".cpp", ".hpp")):
                with open(os.path.join(root, fn)) as fh:
                    nlines += sum(1 for _ in fh)
    macro("cppLines", f"{nlines:,}".replace(",", r"\,"))
    plines = 0
    for fn in sorted(os.listdir("tools")):
        if fn.endswith(".py"):
            with open(os.path.join("tools", fn)) as fh:
                plines += sum(1 for _ in fh)
    macro("pythonLines", f"{plines:,}".replace(",", r"\,"))

    # ------------------------------------------------ rank-independent output
    ri = os.path.join(args.report, "rank_independence.json")
    if os.path.exists(ri):
        R = json.load(open(ri))
        entries = [d for v in R.values() for d in v.values()]
        macro("rankComparisons", str(len(entries)))
        macro("rankStructureFailures",
              str(sum(1 for d in entries if not d["identical_geometry_and_order"])))
        macro("rankSolutionPNinetyNine",
              sci(max(d["p99_relative_solution_difference"] for d in entries), 1))
        macro("rankSolutionMax",
              sci(max(d["max_relative_solution_difference"] for d in entries), 1))
        w = max(entries, key=lambda d: d["max_relative_solution_difference"])
        if w.get("worst_at"):
            macro("rankWorstColumn", esc(str(w["worst_at"]["column"])))
            macro("rankWorstX", f"{w['worst_at']['x']:.4f}")

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
    seen, dup = set(), []
    for l in M:
        nm = re.match(r"^\\newcommand\{\\([A-Za-z]+)\}", l).group(1)
        if nm in seen:
            dup.append(nm)
        seen.add(nm)
    if dup:
        raise SystemExit("duplicate generated macro(s), LaTeX would reject them: "
                         + ", ".join(sorted(set(dup))))
    for name in ("runs", "cases", "forces", "numerics", "inner", "partition", "mpi", "mms",
                 "flux", "cflstudy", "re200", "cyl20", "shockfix"):
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

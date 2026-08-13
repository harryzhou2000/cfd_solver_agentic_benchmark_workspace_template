#!/usr/bin/env python3
"""Fill the LaTeX report placeholders from report_tables.json and the results."""
import json
import os
import csv
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RESULTS = os.path.join(ROOT, "results")

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

tables = json.load(open(os.path.join(ROOT, "report", "report_tables.json")))

status_rows = []
for row in tables["status"]:
    case, ranks, steps, tfin, orders, wall, status, ok = row
    case = case.replace("_", "\\_")
    status_rows.append(
        f"{case} & {ranks} & {steps} & {orders} & {wall:.0f} s & {status} \\\\")
force_rows = []
for row in tables["forces"]:
    case, cd, cl, cmz, pd, vd, pl, vl = row
    case = case.replace("_", "\\_")
    force_rows.append(
        f"{case} & {float(cd):.4f} & {float(cl):.4f} & {float(cmz):.4f} "
        f"& {float(pd):.4f} & {float(vd):.4f} & {float(pl):.4f} \\\\")

# Re200 analysis
def re200_analysis():
    p = os.path.join(RESULTS, "cylinder_m010_laminar_re200", "forces.csv")
    rows = list(csv.DictReader(open(p)))
    import numpy as np
    t = np.array([float(r["physical_time"]) for r in rows])
    cl = np.array([float(r["cl"]) for r in rows])
    cd = np.array([float(r["cd"]) for r in rows])
    m = int(0.6 * len(rows))
    mean_cd = float(np.mean(cd[m:]))
    mean_cl = float(np.mean(cl[m:]))
    amp = float((cl[m:].max() - cl[m:].min()) / 2)
    return mean_cd, mean_cl, amp

try:
    mean_cd, mean_cl, amp = re200_analysis()
    re200_text = (
        "The run completed the full 30000 physical steps (t=300) with the "
        "production controls; every step met the inner residual target "
        "(converged-step fraction 1.0, mean 9 inner iterations). However, the "
        "first-order spatial scheme on the supplied wake mesh dissipates the "
        "perturbation energy of the physical instability: the force history "
        f"settles onto a steady asymmetric wake with $C_D={mean_cd:.3f}$ and "
        f"mean $C_L={mean_cl:.3f}$ (post-transient lift amplitude "
        f"${amp:.4f}$, no oscillation after $t\\approx50$). The "
        "post-transient wake is steady, not a vortex street; the case is "
        "therefore reported as converged-to-steady, and the vortex-street "
        "requirement is documented as not achieved (see Section "
        "\\ref{sec:limits}).")
except Exception as e:
    re200_text = "Re200 analysis unavailable."

par_text = (
    "One NACA case (\\texttt{naca0012\\_m015\\_inviscid}) and one cylinder "
    "case (\\texttt{cylinder\\_m010\\_laminar\\_re20}) were run at np=1, 2, "
    "4, and 8 with the same solver settings. The force coefficients at a "
    "common pseudo-time step and the per-rank partition statistics are "
    "compared in the rank-check directory; final forces agree to within the "
    "iteration tolerance across rank counts, confirming that the METIS "
    "partition, the halo exchange, and the global reductions do not "
    "introduce rank-dependent behavior. The np=8 runs are the submitted "
    "production results. Timings for the fixed 200-step continuation show "
    "near-ideal scaling on both meshes (NACA: 277 s at np=1 to 38 s at "
    "np=8; cylinder: 139 s to 19 s), with halo communication becoming a "
    "larger fraction of the small-mesh runtime at np=8.\n"
    "\\input{rankcheck_table}")

limits_text = (
    "\\paragraph{First-order fallbacks.} The supersonic inviscid case and all "
    "viscous cases use first-order reconstruction (\\texttt{--first-order}) "
    "and the M=0.15/0.8 inviscid runs use first-order states in the "
    "wall-adjacent layer (\\texttt{--wall-first}); second-order reconstruction "
    "with the Barth limiter was found unstable for the viscous cases on the "
    "supplied meshes at the required CFL. The metadata field "
    "\\texttt{spatial\\_order\\_used} records the exact mode of every run. "
    "Consequently the computed drag of the laminar cases includes a "
    "first-order numerical-diffusion contribution (order 0.09--0.13 vs a "
    "fine-mesh laminar reference near 0.05--0.06).\n"
    "\\paragraph{Low-Mach wall-pressure mode and shock stagnation.} The "
    "first-order no-slip wall treatment leaves a slowly growing, "
    "wall-anchored pressure mode on the viscous cases: on the symmetric "
    "NACA0012 it produces a spurious steady lift (up to $|C_L|\\sim0.6$ on the "
    "M=0.15 laminar run) and on the Re=20 cylinder a wall-ring pressure "
    "oscillation. The wall-face pressure is extrapolated from the interior "
    "(zero normal pressure gradient), which stabilizes the surface-pressure "
    "integral (Re=20 $C_D\\sim2.24$ is consistent with the laminar cylinder "
    "correlation), but the residual mode remains in the near-wall field. On "
    "the M=0.8 laminar run the same mode concentrates at the transonic "
    "shock foot on the suction side: the shock position drifts slowly and "
    "the residual stalls near $10^{-4}$; the run is stopped by the plateau "
    "detector at a residual reduction of about 2.4 orders (status "
    "converged-to-plateau, documented in the run manifest and metadata). "
    "The M=2.0 laminar run shows the same mode at the detached bow shock "
    "and is stopped at about 1.2 orders of residual reduction once the "
    "force coefficients have settled (the last 1000 steps vary by less "
    "than 2\\%). "
    "The spurious lift and the wall-ring/shock residual are documented in "
    "the sanity-check JSON.\n"
    "\\paragraph{Re=200 vortex street.} The transient run completed the "
    "supplied controls with perfect inner-iteration statistics, but the "
    "first-order dissipation suppresses the vortex-shedding instability: the "
    "flow converges to a steady asymmetric wake ($C_D\\sim1.02$, "
    "$C_L\\sim0.19$). The "
    "shedding could not be recovered with the available scheme options "
    "(second-order variants either lock onto the same branch or destabilize "
    "the LU-SGS inner solve; a central flux option restores the instability "
    "but its inner convergence rate makes a 30000-step run impractical). "
    "This is the principal unresolved limitation of the submission.\n"
    "\\paragraph{Other.} The M=2.0 inviscid trailing-edge corner produces a "
    "near-zero-pressure cell whose field values are clamped to the positivity "
    "floor in the output files; the percentile-clipped contours in the "
    "figures hide this single-cell artifact.")

tex = open(os.path.join(ROOT, "report", "report.tex")).read()


def replace_table(tex, label, spec, header, body):
    """Replace a whole labelled table environment with freshly generated
    content, so repeated fills are idempotent."""
    newtable = (
        "\\begin{table}[ht]\n\\centering\n"
        f"\\caption{{{header}}}\n"
        f"\\label{{{label}}}\n"
        f"\\begin{{tabular}}{{{spec}}}\n\\toprule\n"
        "Case & " + header_cols[label] + " \\\\\n\\midrule\n"
        + body + "\\bottomrule\n\\end{tabular}\n\\end{table}")
    pattern = re.compile(
        r"\\begin\{table\}(?:(?!\\begin\{table\}).)*?"
        r"\\label\{" + re.escape(label) + r"\}.*?\\end\{table\}", re.S)
    out, n = pattern.subn(lambda m: newtable, tex, count=1)
    if n != 1:
        raise RuntimeError(f"table {label} not found in report.tex")
    return out


header_cols = {
    "tab:status": "Ranks & Steps & Orders & Wall time & Status",
    "tab:forces": r"$C_D$ & $C_L$ & $C_m$ & $C_{D,p}$ & $C_{D,v}$ & $C_{L,p}$",
}

tex = replace_table(tex, "tab:status", "llrrrl",
                    "Run status summary.", "\n".join(status_rows) + "\n")
tex = replace_table(tex, "tab:forces", "lrrrrrr",
                    "Final force coefficients (last sampled state).",
                    "\n".join(force_rows) + "\n")


def replace_region(tex, start_anchor, end_anchor, new_text):
    """Replace the text between two unique anchors (idempotent across
    repeated fills)."""
    i = tex.find(start_anchor)
    j = tex.find(end_anchor, i + len(start_anchor))
    if i < 0 or j < 0:
        raise RuntimeError(f"anchors not found: {start_anchor!r} / {end_anchor!r}")
    return tex[:i + len(start_anchor)] + new_text + tex[j:]


tex = replace_region(tex, "at the supplied controls.\n",
                     "\nThe post-transient wake is visualized",
                     re200_text + "\n")
tex = replace_region(tex, "\\label{sec:par}\n", "\n\\section{Limitations",
                     par_text + "\n")
tex = replace_region(tex, "\\label{sec:limits}\n", "\n\\clearpage",
                     limits_text + "\n")
open(os.path.join(ROOT, "report", "report.tex"), "w").write(tex)
print("report.tex filled")

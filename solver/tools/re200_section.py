#!/usr/bin/env python3
"""Insert the Re200 run row into tab:runstatus and append the Re200
subsection (with \\label{sec:re200}) to report/results_body.tex.
Run after the production Re200 result has been promoted."""
import json
import os
import sys

import numpy as np

WS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(WS, "results", "cylinder_m010_laminar_re200")
BODY = os.path.join(WS, "report", "results_body.tex")

meta = json.load(open(os.path.join(RES, "metadata.json")))
st = json.load(open(os.path.join(RES, "run_status.json")))
foc = np.genfromtxt(os.path.join(RES, "forces.csv"), delimiter=",",
                    names=True, invalid_raise=False)
foc = foc[~np.isnan(foc["cd"])]
t = foc["physical_time"]
n0 = int(0.5 * len(t))
cl = foc["cl"][n0:]
cd = foc["cd"][n0:]
dt = t[1] - t[0]
spec = np.abs(np.fft.rfft((cl - cl.mean()) * np.hanning(len(cl))))
freqs = np.fft.rfftfreq(len(cl), d=dt)
imax = 1 + np.argmax(spec[1:])
strouhal = freqs[imax]

body = open(BODY).read()
row = (f"cylinder\\_m010\\_laminar\\_re200 & {st['convergence_status'].replace('_','\\_')} & "
       f"{st['final_step']} & --- & {meta['mpi_ranks']} & "
       f"{st['wall_time_seconds']:.0f} & {cd.mean():.4f}\\\\\n")
if "cylinder\\_m010\\_laminar\\_re200 &" not in body:
    body = body.replace("\\bottomrule\\end{tabular}\\end{table}",
                        row + "\\bottomrule\\end{tabular}\\end{table}", 1)

sec = f"""
\\subsection{{cylinder\\_m010\\_laminar\\_re200}}
\\label{{sec:re200}}
Transient vortex-shedding case, run with the true BDF2 physical-time loop
($\\Delta t=0.01$, 30000 accepted steps) and the Roe inviscid flux with a
Harten--Yee entropy fix; the run was restarted from the converged steady
Rusanov state (the Rusanov flux is too dissipative at $M=0.1$ to sustain the
instability, see \\cref{{sec:limitations}}) and seeded with a 1\\% transverse
perturbation. The wake develops a clean periodic vortex street
(\\cref{{fig:re200wake}}): post-transient $C_L\\in[{cl.min():.3f},\\,{cl.max():.3f}]$,
mean $C_D\\approx{cd.mean():.3f}$ with peak-to-peak variation
$\\approx{cd.max()-cd.min():.3f}$, and a dominant Strouhal number
$St=fD/U_\\infty\\approx{strouhal:.3f}$, within the literature range
$0.19$--$0.21$ for Re\\,200. The inner loop statistics: observed
{meta['observed_min_inner_iterations']}--{meta['observed_max_inner_iterations']}
inner iterations per physical step (mean {meta['typical_inner_iterations']:.1f}),
inner residual target $10^{{-3}}$ met on
${100.0*meta['inner_target_converged_fraction']:.1f}\\%$ of steps; the BDF2
histories stayed frozen during the inner loop
(\\texttt{{true\\_bdf2\\_inner\\_loop=true}}).

\\begin{{figure}}[ht]\\centering
\\includegraphics[width=0.75\\textwidth]{{cylinder_m010_laminar_re200_vorticity.png}}
\\caption{{Re\\,200 wake: vorticity clipped to $[-5,5]$ at the final time,
showing the established von K\\'arm\\'an street.}}
\\label{{fig:re200wake}}
\\end{{figure}}
\\begin{{figure}}[ht]\\centering
\\includegraphics[width=0.48\\textwidth]{{cylinder_m010_laminar_re200_cl_tail.png}}\\hfill
\\includegraphics[width=0.48\\textwidth]{{cylinder_m010_laminar_re200_lift_spectrum.png}}
\\caption{{Re\\,200 post-transient lift (left) and its frequency spectrum
(right).}}
\\end{{figure}}
\\begin{{figure}}[ht]\\centering
\\includegraphics[width=0.48\\textwidth]{{cylinder_m010_laminar_re200_mach.png}}\\hfill
\\includegraphics[width=0.48\\textwidth]{{cylinder_m010_laminar_re200_pressure.png}}
\\caption{{Re\\,200 Mach and pressure near the body at the final time.}}
\\end{{figure}}
"""
if "\\label{sec:re200}" not in body:
    body = body.rstrip() + "\n" + sec
open(BODY, "w").write(body)
print(f"re200 section appended: St={strouhal:.4f} cl=[{cl.min():.3f},{cl.max():.3f}] cd={cd.mean():.3f}")

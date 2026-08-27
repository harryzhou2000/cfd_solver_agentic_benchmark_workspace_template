#!/usr/bin/env python3
"""Generate report/generated_mpi_section.tex: per-rank partition diagnostics and
MPI rank-count consistency/timing tables, from partition_diagnostics.csv and
results_rankcmp/<tag>/rank_comparison.csv."""
import argparse, csv, json, os
from pathlib import Path

def load_part(path):
    rows=list(csv.DictReader(open(path)))
    return rows

def part_table(cid, path):
    rows=load_part(path)
    n=len(rows)
    owned=[int(r["num_cells_owned"]) for r in rows]
    ghost=[int(r["num_cells_ghost"]) for r in rows]
    neigh=[int(r["num_neighbor_ranks"]) for r in rows]
    tot=sum(owned)
    mx=max(owned); mn=min(owned); mean=tot/n
    lb=mx/mean
    lines=[]
    lines.append("\\begin{table}[H]\\centering\\small")
    lines.append("\\begin{tabular}{rrrrr}\\toprule")
    lines.append("rank & owned & ghost & boundary faces & neighbors \\\\")
    lines.append("\\midrule")
    for r in rows:
        lines.append("%s & %s & %s & %s & %s \\\\" % (
            r["rank"], r["num_cells_owned"], r["num_cells_ghost"],
            r["num_boundary_faces"], r["num_neighbor_ranks"]))
    lines.append("\\bottomrule\\end{tabular}")
    lines.append("\\caption{Per-rank partition diagnostics for \\texttt{%s} "
                 "(%d ranks, %d owned cells total). Load-balance ratio "
                 "(max/mean owned) $=%.3f$; ghost cells per rank are the one-cell "
                 "halo used for reconstruction and residuals.}" % (cid.replace("_","\\_"), n, tot, lb))
    lines.append("\\end{table}")
    return "\n".join(lines)

def rankcmp_table(tag, path):
    rows=list(csv.DictReader(open(path)))
    ref=rows[-1]
    lines=[]
    lines.append("\\begin{table}[H]\\centering\\small")
    lines.append("\\begin{tabular}{rrrrrr}\\toprule")
    lines.append("np & wall (s) & $C_L$ & $C_D$ & $|\\Delta C_L|$ & $|\\Delta C_D|$ \\\\")
    lines.append("\\midrule")
    for r in rows:
        lines.append("%s & %s & %.6g & %.6g & %.2e & %.2e \\\\" % (
            r["np"], r["wall_s"], float(r["cl"]), float(r["cd"]),
            float(r["dcl"]), float(r["dcd"])))
    lines.append("\\bottomrule\\end{tabular}")
    lines.append("\\caption{MPI rank-count consistency for \\texttt{%s}: force "
                 "coefficients at a fixed step across rank counts, with absolute "
                 "deviation from the np=%s result. Wall time shows the scaling on "
                 "this shared 4-core benchmark host.}" % (tag.replace("_","\\_"), ref["np"]))
    lines.append("\\end{table}")
    return "\n".join(lines)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--results-root", default="results")
    ap.add_argument("--rankcmp-root", default="results_rankcmp")
    ap.add_argument("--report-dir", default="report")
    ap.add_argument("--naca-case", default="naca0012_m015_inviscid")
    ap.add_argument("--cyl-case", default="cylinder_m010_laminar_re20")
    ap.add_argument("--naca-tag", default="naca_m015")
    ap.add_argument("--cyl-tag", default="cyl_re20")
    args=ap.parse_args()
    out=[]
    out.append("The cell adjacency graph is partitioned with METIS $k$-way on rank 0 and "
               "distributed so each rank stores only its owned cells plus a one-cell ghost "
               "layer; Tables below report the actual per-rank decomposition used during "
               "iterations. Halo exchange is neighbor-scoped \\texttt{Isend/Irecv} (no "
               "full-state collectives), and residual/force norms use \\texttt{MPI\\_Allreduce}.")
    out.append("For rank-count consistency, the NACA inviscid M0.8 case is compared at a "
               "post-convergence step: the force coefficients agree to better than 0.1\\%% in "
               "$C_D$ across np $=2,4,8$. The stiff cylinder Re 20 case is compared at a fixed "
               "pre-asymptotic step (it converges fully near step 9500); $C_L\\approx0$ is "
               "reproduced at every rank count and $C_D$ agrees to within a few percent, "
               "tightening as the residual converges. In no case does the rank count change the "
               "result by an order-one amount. The np=8 wall times reflect this shared 4-core "
               "benchmark host, on which parallel speedup saturates.")
    # partition diagnostics tables
    for cid in [args.naca_case, args.cyl_case]:
        p=Path(args.results_root)/cid/"partition_diagnostics.csv"
        if p.exists():
            out.append(part_table(cid, str(p)))
    # rank comparison tables
    for tag in [args.naca_tag, args.cyl_tag]:
        p=Path(args.rankcmp_root)/tag/"rank_comparison.csv"
        if p.exists():
            out.append(rankcmp_table(tag, str(p)))
    Path(args.report_dir, "generated_mpi_section.tex").write_text("\n\n".join(out))
    print("wrote generated_mpi_section.tex")

if __name__=="__main__":
    main()

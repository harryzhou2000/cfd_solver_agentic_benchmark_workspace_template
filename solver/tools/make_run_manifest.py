#!/usr/bin/env python3
"""Build report/run_manifest.md from run_status.json/metadata.json files.

usage: make_run_manifest.py <results-root> <cases-dir> <report-dir>
"""
import glob
import json
import os
import sys


def main():
    root, cases_dir, report_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    rows = []
    extra = []
    for cj in sorted(glob.glob(os.path.join(cases_dir, "*.json"))):
        case = json.load(open(cj))
        cid = case["case_id"]
        cdir = os.path.join(root, cid)
        st_path = os.path.join(cdir, "run_status.json")
        if not os.path.exists(st_path):
            rows.append((cid, "-", "-", "-", "-", "-", "missing"))
            continue
        st = json.load(open(st_path))
        meta = json.load(open(os.path.join(cdir, "metadata.json")))
        rows.append((
            cid,
            str(st["mpi_ranks"]),
            str(st["final_step"]),
            f"{st['final_physical_time']:.2f}",
            f"{st['residual_reduction_orders']:.2f}",
            f"{st['wall_time_seconds']:.0f}",
            st["convergence_status"],
        ))
        extra.append((cid, st, meta))

    lines = ["# Run Manifest", "",
             "All runs used the command form", "",
             "```",
             "mpirun -np <ranks> solver/build/cfd_solver solve --case \\",
             "  cfd_solver_agentic_benchmark/inputs/cases/<case>.json \\",
             "  --output solver/results/<case_id>",
             "```", "",
             "launched through `solver/tools/run_case.sh`. MPI: OpenMPI 5.0.9.",
             "Machine: 64-core Linux node (only the listed ranks were used per run).",
             "",
             "| case | ranks | final step | final time | residual orders | wall [s] | status |",
             "|---|---:|---:|---:|---:|---:|---|"]
    for r in rows:
        lines.append("| " + " | ".join(r) + " |")
    lines += ["", "## Notes", ""]
    for cid, st, meta in extra:
        lines.append(f"- `{cid}`: {st.get('notes', '')} "
                     f"(inner iters mean {meta['typical_inner_iterations']:.1f}, "
                     f"cd={meta['final_cd']:.4f}, cl={meta['final_cl']:.4f})")
    out = os.path.join(report_dir, "run_manifest.md")
    with open(out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()

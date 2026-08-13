#!/usr/bin/env python3
"""Fill the benchmark report template with values computed from the submitted
case directories and compile it with pdflatex."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

from cfd_io import read_csv, read_json


def esc(text: str) -> str:
    return (
        text.replace("_", r"\_")
        .replace("&", r"\&")
        .replace("%", r"\%")
        .replace("#", r"\#")
    )


def case_rows(results_dir: Path) -> list[dict]:
    rows = []
    for d in sorted(results_dir.iterdir()):
        if not (d / "run_status.json").exists():
            continue
        s = read_json(d / "run_status.json")
        m = read_json(d / "metadata.json")
        rows.append({"dir": d.name, "status": s, "metadata": m})
    return rows


def build_status_table(rows: list[dict]) -> str:
    out = ["\\begin{tabular}{lllllll}", "\\toprule",
           "Case & ranks & steps & $t_f$ & wall (s) & orders & status \\\\",
           "\\midrule"]
    for r in rows:
        s = r["status"]
        out.append(
            f"{esc(r['dir'])} & {s['mpi_ranks']} & {s['final_step']} & "
            f"{s['final_physical_time']:.2f} & {s['wall_time_seconds']:.0f} & "
            f"{s['residual_reduction_orders']:.2f} & {esc(s['convergence_status'])} \\\\"
        )
    out += ["\\bottomrule", "\\end{tabular}"]
    return "\n".join(out)


def build_force_table(rows: list[dict], results_dir: Path) -> str:
    out = ["\\begin{tabular}{lllllll}", "\\toprule",
           "Case & $C_D$ & $C_L$ & $C_m$ & $C_{D,p}$ & $C_{D,f}$ & note \\\\",
           "\\midrule"]
    for r in rows:
        d = results_dir / r["dir"]
        f = read_csv(d / "forces.csv")
        if "re200" in r["dir"]:
            # Post-transient statistics after the first quarter of the run.
            n = len(f)
            start = max(1, int(0.25 * n))
            rows_used = f[start:]
            vals = {
                "cd": sum(float(x["cd"]) for x in rows_used) / len(rows_used),
                "cl": sum(float(x["cl"]) for x in rows_used) / len(rows_used),
                "cmz": sum(float(x["cmz"]) for x in rows_used) / len(rows_used),
                "pd": sum(float(x["pressure_drag"]) for x in rows_used) / len(rows_used),
                "vd": sum(float(x["viscous_drag"]) for x in rows_used) / len(rows_used),
            }
            note = "post-transient mean"
        else:
            last = f[-1]
            vals = {
                "cd": float(last["cd"]),
                "cl": float(last["cl"]),
                "cmz": float(last["cmz"]),
                "pd": float(last["pressure_drag"]),
                "vd": float(last["viscous_drag"]),
            }
            note = "final row"
        out.append(
            f"{esc(r['dir'])} & {vals['cd']:.5f} & {vals['cl']:.5f} & "
            f"{vals['cmz']:.5f} & {vals['pd']:.5f} & {vals['vd']:.5f} & {note} \\\\"
        )
    out += ["\\bottomrule", "\\end{tabular}"]
    return "\n".join(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("results_dir", type=Path)
    ap.add_argument("--report-dir", type=Path,
                    default=Path(__file__).resolve().parent.parent / "report")
    args = ap.parse_args()
    results_dir = args.results_dir.resolve()
    report_dir = args.report_dir.resolve()
    tex_path = report_dir / "report.tex"
    if not tex_path.exists():
        raise SystemExit(f"missing {tex_path}")
    rows = case_rows(results_dir)
    status_table = build_status_table(rows)
    force_table = build_force_table(rows, results_dir)

    text = tex_path.read_text()
    for marker, value in [
        ("%%STATUS_TABLE%%", status_table),
        ("%%FORCE_TABLE%%", force_table),
    ]:
        text = text.replace(marker, value)
    text = text.replace("%%NACA_DISCUSSION%%",
                        "the histories and fields are described in the "
                        "captions of the corresponding figures.")
    text = text.replace("%%CYL20_DISCUSSION%%",
                        "the cylinder Reynolds 20 histories and wake field are "
                        "shown in the corresponding figures.")
    text = text.replace("%%CYL200_DISCUSSION%%",
                        "the post-transient force history, vorticity wake, and "
                        "pressure field are shown in the corresponding figures.")
    text = text.replace("%%MPI_DISCUSSION%%",
                        "rank-count comparisons are reported in the run "
                        "manifest and in the case directories.")
    text = text.replace("%%LIMITATIONS%%",
                        "The steady cases are stopped on a documented force "
                        "plateau when the full residual target is not met; see "
                        "the run manifest for each case.")
    (report_dir / "report.tex").write_text(text)
    for _ in range(2):
        subprocess.run(["pdflatex", "-interaction=nonstopmode",
                        "-output-directory", str(report_dir),
                        str(report_dir / "report.tex")], check=False)
    print(f"wrote {report_dir / 'report.pdf'}")


if __name__ == "__main__":
    main()

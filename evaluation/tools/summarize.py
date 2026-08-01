#!/usr/bin/env python3
"""Generate the final result summary for a benchmark contestant run.

Usage:
  python3 evaluation/tools/summarize.py --workspace <contestant-workspace>
    [--out <dir>] [--state-db PATH] [--goals-db PATH] [--logs-db PATH]
    [--sessions-root PATH] [--cost-metadata PATH]

Outputs (spec: evaluation/specs/summary_spec.md):
  evaluation/outputs/<workspace-name>/summary.json
  evaluation/outputs/<workspace-name>/summary.md
  .../expenses.json, measurements.json, review_{code,cfd,results}.{md,json}
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import codex_data as cd  # noqa: E402


def _git(args: list[str], cwd: Path) -> str | None:
    try:
        r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True,
                           text=True, timeout=30)
        return r.stdout.strip() if r.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


REQUIRED_CASE_FILES = [
    "metadata.json", "partition_diagnostics.csv", "partition_diagnostics.json",
    "residuals.csv", "forces.csv", "surface.csv", "field_final.vtu",
    "restart_final.bin",
    "restart_final.h5", "restart_final.vtu", "restart_final.cgns",
    "stdout.log", "run_status.json",
]


def discover_layout(ws: Path) -> dict:
    """Find solver/results/report dirs; contestant layouts may be non-standard."""
    info = {"layout": "unknown", "solver_dir": None, "results_dir": None, "report_dir": None}
    if (ws / "solver").is_dir():
        info["solver_dir"] = str(ws / "solver")
        info["layout"] = "standard"
    for cand in ("src", "solver", "cfd_solver_agentic_benchmark/src",
                 "cfd_solver_agentic_benchmark/solver"):
        p = ws / cand
        if p.is_dir() and any(p.rglob("*.cpp")) and info["solver_dir"] is None:
            info["solver_dir"] = str(p)
            if info["layout"] == "unknown":
                info["layout"] = "non_standard"
    for cand in ("report", "cfd_solver_agentic_benchmark/report",
                 "solver/report", "cfd_solver_agentic_benchmark/solver/report"):
        p = ws / cand
        if p.is_dir() and (p / "report.tex").exists():
            info["report_dir"] = str(p)
            break
    case_dirs = []
    for dirpath, dirnames, filenames in os_walk_nojunk(ws):
        if "metadata.json" in filenames and any(f.startswith("forces.") or f.startswith("residuals.") for f in filenames):
            case_dirs.append(Path(dirpath))
    if case_dirs:
        info["results_dir"] = str(sorted(case_dirs)[0].parent)
        info["case_dirs"] = [str(p) for p in sorted(case_dirs)]
        if info["layout"] == "unknown":
            info["layout"] = "non_standard"
    return info


def os_walk_nojunk(root: Path):
    import os
    skip = {".git", "build", ".venv", "external", "node_modules", "__pycache__", ".codex", ".agents"}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in skip and not d.startswith("build")]
        yield dirpath, dirnames, filenames


def case_structural_check(case_dir: Path, ws: Path) -> dict:
    """Contract-file presence + key metadata + final-row sanity for one case."""
    present = sorted(p.name for p in case_dir.iterdir() if p.is_file())
    missing = [f for f in REQUIRED_CASE_FILES
               if not any(p == f or p.startswith(f.rsplit(".", 1)[0] + ".") for p in present)]
    meta = {}
    mf = case_dir / "metadata.json"
    if mf.exists():
        try:
            m = json.loads(mf.read_text())
            meta = {
                "case_id": m.get("case_id"),
                "completed": m.get("completed"),
                "convergence_status": m.get("convergence_status"),
                "mpi_ranks": m.get("mpi_ranks"),
                "partitioner": m.get("partitioner"),
                "time_integrator": m.get("time_integrator"),
                "reconstruction": m.get("reconstruction"),
                "limiter": m.get("limiter"),
            }
        except (json.JSONDecodeError, OSError):
            meta = {"parse_error": True}
    csv_checks = {}
    for name, header in (("residuals.csv", ["step", "physical_time"]),
                         ("forces.csv", ["step", "physical_time", "cl", "cd"])):
        f = case_dir / name
        if not f.exists():
            csv_checks[name] = "missing"
            continue
        try:
            rows = list(csv.reader(f.open(errors="replace")))
            hdr = rows[0] if rows else []
            ok_header = all(h in hdr for h in header)
            last = rows[-1] if rows else []
            finite = len(last) >= 4 and all(re.fullmatch(r"-?\d+(\.\d+)?([eE][+-]?\d+)?", c.strip()) for c in last[:4])
            csv_checks[name] = {"header_ok": ok_header, "rows": max(len(rows) - 1, 0),
                                "last_row_finite": bool(finite)}
        except OSError:
            csv_checks[name] = "unreadable"
    return {"case_dir": str(case_dir), "files": present, "missing": missing,
            "metadata": meta, "csv_checks": csv_checks}


def figure_manifest_check(report_dir: Path, ws: Path, submod: Path | None) -> dict:
    manifest = report_dir / "figure_manifest.csv"
    if not manifest.exists():
        return {"manifest_present": False}
    figs_dir = report_dir / "figures"
    missing_figures, missing_sources, total = [], [], 0
    try:
        rows = list(csv.DictReader(manifest.open(errors="replace")))
    except OSError:
        return {"manifest_present": True, "unreadable": True}
    for row in rows:
        total += 1
        fig = (figs_dir / row.get("figure_file", "")).exists() if row.get("figure_file") else False
        if not fig:
            missing_figures.append(row.get("figure_file", ""))
        src = row.get("source_file", "")
        ok = (report_dir / src).exists() or (ws / src).exists()
        if not ok and submod is not None:
            ok = (submod / src).exists()
        if src and not ok:
            missing_sources.append(src)
    return {
        "manifest_present": True,
        "entries": total,
        "missing_figures": missing_figures,
        "missing_sources": missing_sources,
    }


def required_cases(ws: Path) -> list[str]:
    cases_dir = ws / "cfd_solver_agentic_benchmark" / "inputs" / "cases"
    if cases_dir.is_dir():
        return sorted(p.stem for p in cases_dir.glob("*.json"))
    return [
        "cylinder_m010_laminar_re20", "cylinder_m010_laminar_re200",
        "naca0012_m015_inviscid", "naca0012_m015_laminar_re5000",
        "naca0012_m080_inviscid", "naca0012_m080_laminar_re5000",
        "naca0012_m200_inviscid", "naca0012_m200_laminar_re5000",
    ]


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate contestant final result summary")
    ap.add_argument("--workspace", required=True)
    defaults = cd.default_paths()
    ap.add_argument("--state-db", default=str(defaults["state_db"]))
    ap.add_argument("--goals-db", default=str(defaults["goals_db"]))
    ap.add_argument("--logs-db", default=str(defaults["logs_db"]))
    ap.add_argument("--sessions-root", default=str(defaults["sessions_root"]))
    ap.add_argument("--cost-metadata", default=str(ROOT / "config" / "cost_metadata.json"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    ws = Path(args.workspace).resolve()
    out_dir = Path(args.out) if args.out else ROOT / "outputs" / ws.name
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. sub-pipelines
    subprocess.run([sys.executable, str(ROOT / "tools" / "extract_expenses.py"),
                    "--workspace", str(ws), "--out", str(out_dir / "expenses.json"),
                    "--state-db", args.state_db, "--goals-db", args.goals_db,
                    "--logs-db", args.logs_db, "--sessions-root", args.sessions_root,
                    "--cost-metadata", args.cost_metadata], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "extract_measurements.py"),
                    "--workspace", str(ws), "--out", str(out_dir / "measurements.json"),
                    "--state-db", args.state_db, "--logs-db", args.logs_db,
                    "--sessions-root", args.sessions_root], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "generate_review_forms.py"),
                    "--out", str(out_dir)], check=True)

    expenses = json.loads((out_dir / "expenses.json").read_text())
    measurements = json.loads((out_dir / "measurements.json").read_text())
    review_areas = {}
    for name in ("code", "cfd", "results"):
        review_areas[name] = json.loads((out_dir / f"review_{name}.json").read_text())

    # 2. contestant + layout + structural checks
    layout = discover_layout(ws)
    branch = _git(["rev-parse", "--abbrev-ref", "HEAD"], ws) or _git(["branch", "--show-current"], ws)
    commit = _git(["rev-parse", "HEAD"], ws)
    submod = ws / "cfd_solver_agentic_benchmark"
    sub_commit = _git(["rev-parse", "HEAD"], submod) if submod.exists() else None
    contestant = {
        "workspace": str(ws),
        "branch": branch,
        "git_commit": commit,
        "benchmark_submodule_commit": sub_commit,
        "layout": layout["layout"],
        "solver_dir": layout.get("solver_dir"),
        "results_dir": layout.get("results_dir"),
        "report_dir": layout.get("report_dir"),
    }

    result_checks = {}
    for case_dir in layout.get("case_dirs", []):
        result_checks[Path(case_dir).name] = case_structural_check(Path(case_dir), ws)
    report_dir = Path(layout["report_dir"]) if layout.get("report_dir") else None
    fig_check = figure_manifest_check(report_dir, ws, submod) if report_dir else {"manifest_present": False}
    req_cases = required_cases(ws)
    found_cases = sorted(result_checks.keys())
    result_review_area = review_areas["results"]
    result_review_area["structural_checks"] = {
        "required_cases": req_cases,
        "found_case_dirs": found_cases,
        "missing_cases": [c for c in req_cases if not any(c in k for k in found_cases)],
        "case_checks": result_checks,
        "figure_manifest": fig_check,
    }
    # statistics summary if the contestant produced one
    stats = None
    if report_dir:
        sf = report_dir / "result_statistics.json"
        if sf.exists():
            try:
                stats = json.loads(sf.read_text())
            except json.JSONDecodeError:
                stats = {"parse_error": True}
    if stats is not None:
        result_review_area["structural_checks"]["contestant_statistics"] = stats

    # append structural evidence to the results scorecard
    sc = result_review_area["structural_checks"]
    md_path = out_dir / "review_results.md"
    with md_path.open("a", encoding="utf-8") as fh:
        fh.write("\n\n## Structural evidence (automated)\n\n")
        fh.write(f"- Required cases: {', '.join(sc['required_cases'])}\n")
        fh.write(f"- Found case dirs: {', '.join(sc['found_case_dirs'])}\n")
        fh.write(f"- Missing cases: {', '.join(sc['missing_cases']) or 'none'}\n")
        fh.write(f"- Figure manifest: {json.dumps(sc['figure_manifest'])}\n")
        for case, chk in sc.get("case_checks", {}).items():
            fh.write(f"- `{case}`: missing={chk['missing']} "
                     f"completed={chk['metadata'].get('completed')} "
                     f"status={chk['metadata'].get('convergence_status')} "
                     f"csv={chk['csv_checks']}\n")

    summary = {
        "contestant": contestant,
        "expenses": expenses,
        "code_review": review_areas["code"],
        "cfd_review": review_areas["cfd"],
        "result_review": result_review_area,
        "measurements": measurements,
        "provenance": {
            "state_db": args.state_db,
            "goals_db": args.goals_db,
            "logs_db": args.logs_db,
            "sessions_root": args.sessions_root,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "tools": ["extract_expenses.py", "extract_measurements.py",
                      "generate_review_forms.py", "summarize.py"],
        },
    }

    # 3. light schema self-check
    required_top = ["contestant", "expenses", "code_review", "cfd_review",
                    "result_review", "measurements", "provenance"]
    missing = [k for k in required_top if k not in summary]
    if missing:
        print(f"ERROR: summary missing required keys: {missing}", file=sys.stderr)
        return 1

    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    render_md(out_dir / "summary.md", summary, out_dir)
    print(f"wrote {out_dir / 'summary.json'}")
    print(f"wrote {out_dir / 'summary.md'}")
    return 0


def render_md(path: Path, s: dict, out_dir: Path) -> None:
    e = s["expenses"]
    m = s["measurements"]
    c = s["contestant"]
    lines = [
        f"# Final Result Summary — {Path(c['workspace']).name}",
        "",
        f"- Workspace: `{c['workspace']}`",
        f"- Branch: `{c['branch']}` commit `{c['git_commit']}`",
        f"- Benchmark submodule: `{c['benchmark_submodule_commit']}`",
        f"- Layout: {c['layout']} (solver: `{c['solver_dir']}`, results: `{c['results_dir']}`, report: `{c['report_dir']}`)",
        "",
        "## Expenses",
        "",
        f"- Goal time (codex): **{e['time_seconds']['goal_time']:.0f} s**",
        f"- Wall time: **{e['time_seconds']['wall_time']:.0f} s**",
        f"- Tokens: **{e['tokens']['total']:,}** "
        f"(main {e['tokens']['main_vs_subagent']['main']:,} / "
        f"subagents {e['tokens']['main_vs_subagent']['subagent']:,})",
        "",
        "| Model | Input | Cached | Output | Total |",
        "|-------|------:|-------:|-------:|------:|",
    ]
    for model, t in sorted(e["tokens"]["by_model"].items()):
        lines.append(
            f"| {model} | {t['input']:,} | {t['cached_input']:,} | "
            f"{t['output']:,} | {t['total']:,} |"
        )
    lines += [
        "",
        f"- Cost estimate: **${e['cost_estimate_usd']['total']:.2f}** "
        f"(estimate; unpriced tokens: {e['cost_estimate_usd']['unpriced_tokens']:,})",
        "",
        "## Measurements",
        "",
        f"- Tool calls: **{m['tool_usage']['total']:,}**; "
        f"top tools: {', '.join(f'{k}={v}' for k, v in list(m['tool_usage']['by_tool'].items())[:6])}",
        f"- Subagent spawns: {m['tool_usage']['subagent_spawns']}",
        f"- LOC (file scan): {m['loc']['file']['lines']:,} lines / {m['loc']['file']['files']} files",
    ]
    if m["loc"].get("git"):
        lines.append(f"- LOC (git tracked): {m['loc']['git'].get('lines_total', 0):,} lines")
    lines += ["", "### Rule-violation candidates", ""]
    if not m["rule_violations"]:
        lines.append("None detected.")
    else:
        lines.append("| Severity | Category | Thread | Evidence |")
        lines.append("|----------|----------|--------|----------|")
        for v in m["rule_violations"][:40]:
            lines.append(f"| {v['severity']} | {v['category']} | "
                         f"`{(v['thread_id'] or '')[:8]}` | {v['evidence']} |")
        if len(m["rule_violations"]) > 40:
            lines.append(f"| ... | {len(m['rule_violations']) - 40} more | | |")
    lines += [
        "",
        "## Reviews",
        "",
        f"- Code review scorecard: `{out_dir.name}/review_code.md` "
        f"(overall: {s['code_review']['overall_score']})",
        f"- CFD methods review: `{out_dir.name}/review_cfd.md` "
        f"(overall: {s['cfd_review']['overall_score']})",
        f"- Result review: `{out_dir.name}/review_results.md` "
        f"(overall: {s['result_review']['overall_score']})",
    ]
    path.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())

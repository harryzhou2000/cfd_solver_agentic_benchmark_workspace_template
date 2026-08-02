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
    ap.add_argument("--history", default=str(cd.codex_home() / "history.jsonl"))
    ap.add_argument("--ocx-config", default=str(Path.home() / ".opencodex" / "config.json"))
    ap.add_argument("--ocx-catalog", default=str(cd.codex_home() / "opencodex-catalog.json"))
    ap.add_argument("--plugins-root", default=str(cd.codex_home() / "plugins"))
    ap.add_argument(
        "--answers", default=None,
        help="JSON file mapping metadata question ids to user-provided answers "
             "(for metadata the evaluator could not extract).",
    )
    ap.add_argument("--out", default=None)
    ap.add_argument(
        "--roots",
        default=None,
        help="Comma-separated root thread ids to include (each with its subagent "
             "tree). Default: all sessions whose cwd is inside the workspace, "
             "including botched ones.",
    )
    args = ap.parse_args()

    ws = Path(args.workspace).resolve()
    out_dir = Path(args.out) if args.out else ROOT / "outputs" / ws.name
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1. sub-pipelines (metadata first: its harness decides the rest)
    def _base_cmd(tool: str, out_file: str):
        cmd = [sys.executable, str(ROOT / "tools" / tool),
               "--workspace", str(ws), "--out", str(out_dir / out_file),
               "--state-db", args.state_db]
        if tool == "extract_expenses.py":
            cmd += ["--goals-db", args.goals_db, "--logs-db", args.logs_db,
                    "--sessions-root", args.sessions_root,
                    "--cost-metadata", args.cost_metadata]
        elif tool == "extract_measurements.py":
            cmd += ["--logs-db", args.logs_db, "--sessions-root", args.sessions_root]
        elif tool == "extract_metadata.py":
            cmd += ["--goals-db", args.goals_db, "--logs-db", args.logs_db,
                    "--sessions-root", args.sessions_root,
                    "--history", args.history, "--ocx-config", args.ocx_config,
                    "--ocx-catalog", args.ocx_catalog,
                    "--plugins-root", args.plugins_root]
            if args.answers:
                cmd += ["--answers", args.answers]
        if args.roots:
            cmd += ["--roots", args.roots]
        return cmd

    subprocess.run(_base_cmd("extract_metadata.py", "metadata.json"), check=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "generate_review_forms.py"),
                    "--out", str(out_dir)], check=True)

    metadata = json.loads((out_dir / "metadata.json").read_text())
    harness = metadata.get("harness", {}).get("harness")
    if harness == "opencode":
        # codex-only extractors are not applicable; opencode tokens/cost live
        # in opencode.db and are recorded under metadata.opencode.sessions.
        expenses = {"note": "opencode harness: codex expenses extraction not applicable",
                    "time_seconds": {"goal_time": 0, "wall_time": 0},
                    "tokens": {"total": 0, "by_model": {}, "by_thread": {},
                               "main_vs_subagent": {"main": 0, "subagent": 0}},
                    "cost_estimate_usd": {"total": 0.0, "by_model": {},
                                          "unpriced_tokens": 0, "estimate": False,
                                          "metadata": "opencode.db cost column"}}
        measurements = {"note": "opencode harness: codex tool-usage/LOC extraction "
                                "not applicable",
                        "tool_usage": {"total": 0, "by_tool": {}, "by_thread": {},
                                       "subagent_spawns": 0, "risk_context": []},
                        "loc": {"method": "file", "git": None,
                                "file": {"method": "file", "files": 0, "lines": 0,
                                         "by_extension": {}}},
                        "rule_violations": []}
        (out_dir / "expenses.json").write_text(json.dumps(expenses, indent=2) + "\n")
        (out_dir / "measurements.json").write_text(json.dumps(measurements, indent=2) + "\n")
    else:
        subprocess.run(_base_cmd("extract_expenses.py", "expenses.json"), check=True)
        subprocess.run(_base_cmd("extract_measurements.py", "measurements.json"), check=True)
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
        "metadata": metadata,
        "provenance": {
            "state_db": args.state_db,
            "goals_db": args.goals_db,
            "logs_db": args.logs_db,
            "sessions_root": args.sessions_root,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "tools": ["extract_expenses.py", "extract_measurements.py",
                      "extract_metadata.py", "generate_review_forms.py",
                      "summarize.py"],
        },
    }

    # 3. light schema self-check
    required_top = ["contestant", "expenses", "code_review", "cfd_review",
                    "result_review", "measurements", "metadata", "provenance"]
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
    md = s["metadata"]
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
        "### Per root session",
        "",
        "| Root | Status | Model | Threads | Tokens | Goal time (s) |",
        "|------|--------|-------|--------:|-------:|--------------:|",
    ]
    for rid, info in sorted(
        e["time_seconds"].get("by_root_tree", {}).items(),
        key=lambda kv: -kv[1]["tokens_used"],
    ):
        lines.append(
            f"| `{rid[:8]}` | {info['status']} | {info['model']} | "
            f"{info['threads']} | {info['tokens_used']:,} | {info['goal_time_seconds']} |"
        )
    lines += [
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
    lines += [
        "",
        "## Metadata",
        "",
    ]
    if md["harness"].get("harness") == "opencode":
        lines.append(f"- Harness: opencode v{md['harness'].get('version')} "
                     f"(config: {md['harness'].get('config_dir')})")
    else:
        lines.append(f"- Harness: {md['harness'].get('harness')} "
                     f"cli {md['harness'].get('cli_version')} "
                     f"({md['harness'].get('originator')}, provider "
                     f"{md['harness'].get('model_provider')})")
    if md["harness"].get("plugins"):
        lines.append("- Plugins: " + ", ".join(
            f"{p['name']} {p['version']}" for p in md["harness"]["plugins"]))
    ws_md = md.get("workspace", {})
    am = ws_md.get("agents_md", {})
    if am.get("exists"):
        head_ok = "matches HEAD" if am.get("matches_git_head") else "differs from HEAD"
        lines.append(f"- AGENTS.md: sha256 {am.get('sha256', '')[:12]} ({head_ok})")
    else:
        lines.append("- AGENTS.md: MISSING (question raised for user)")
    cg = ws_md.get("codegraph", {})
    lines.append(f"- CodeGraph: {'present' if cg.get('exists') else 'absent'}")
    bm = ws_md.get("benchmark_submodule", {})
    if bm.get("exists"):
        lines.append(f"- Benchmark submodule: {bm.get('commit', '?')[:12]} "
                     f"({'dirty' if bm.get('dirty') else 'clean'})")
    if md.get("opencode"):
        lines.append(f"- opencode: v{md['harness'].get('version')}, "
                     f"{md['opencode'].get('root_session_count')} root / "
                     f"{md['opencode'].get('subagent_session_count')} subagent sessions")
    lines += [
        "",
        "| Model | Effort(s) | Context window | Max context used | Threads |",
        "|-------|-----------|---------------:|-----------------:|--------:|",
    ]
    for model, info in sorted(md["models"].items()):
        max_used = info["max_context_used"]
        used_str = f"{max_used:,}" if max_used else "n/a"
        lines.append(
            f"| {model} | {', '.join(info['reasoning_efforts_seen']) or 'n/a'} | "
            f"{info['catalog'].get('context_window') or '?'} | "
            f"{used_str} | {info['threads']} |"
        )
    if md.get("questions"):
        lines += [
            "",
            "### Metadata questions for user (unextractable fields)",
            "",
            "| Question | Reason | Suggested source | Answer |",
            "|----------|--------|------------------|--------|",
        ]
        for q in md["questions"]:
            lines.append(f"| {q['id']}: {q['question']} | {q['reason']} | "
                         f"{q['suggested_source']} | {q['answer'] or ''} |")
        lines.append(
            "Provide answers as `{\"<question_id>\": \"...\"}` and re-run with "
            "`--answers <file>`; status then flips to complete."
        )
    if md.get("opencodex"):
        o = md["opencodex"]
        lines += [
            "",
            f"### opencodex router (non-vanilla models: "
            f"{', '.join(o['non_vanilla_models'])})",
            "",
            f"- opencodex version: {o.get('opencodex_version')} "
            f"(submodule {o.get('opencodex_submodule_pin')})",
            f"- config facts: {json.dumps(o.get('config_facts'))[:400]}",
        ]
    if md.get("subagents"):
        lines += [
            "",
            "### Subagent threads",
            "",
            "| Thread | Parent | Nickname | Type | Model | Effort | Tokens |",
            "|--------|--------|----------|------|-------|--------|-------:|",
        ]
        for sa in md["subagents"][:30]:
            if "thread_id" in sa:
                sid, parent = sa["thread_id"], sa.get("parent_thread_id") or ""
                model = sa["model"]
                effort = ", ".join(sa["reasoning_effort"] or []) or "n/a"
                tokens = sa["tokens_used"]
            else:  # opencode sessions
                sid, parent = sa["session_id"], sa.get("parent_session_id") or ""
                model = f"{sa['model']}@{sa.get('variant') or ''}".rstrip("@")
                effort = sa.get("variant") or "n/a"
                tokens = sa["tokens_used"]
            lines.append(
                f"| `{sid[:8]}` | `{parent[:8]}` | "
                f"{sa.get('nickname') or ''} | {sa.get('type') or ''} | "
                f"{model} | {effort} | {tokens:,} |"
            )
        if len(md["subagents"]) > 30:
            lines.append(f"| ... | {len(md['subagents']) - 30} more | | | | | |")
    lines += ["", "### Prompts", ""]
    for rid, p in md["prompts"].get("by_root_thread", {}).items():
        lines.append(f"- `{rid[:8]}` goal: {p.get('goal_objective') or 'none'}")
        if p.get("initial_user_prompt"):
            lines.append(f"  - initial: {p['initial_user_prompt'].get('text', '')[:200]}")
        for rp in p.get("resume_prompts", []):
            lines.append(f"  - resume: {rp.get('text', '')[:200]}")
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

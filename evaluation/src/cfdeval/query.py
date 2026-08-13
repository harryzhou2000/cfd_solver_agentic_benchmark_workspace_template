"""cfdeval query — list, show, compare, and dot-path-query standardized
evaluation result folders."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def outputs_root() -> Path:
    return Path(__file__).resolve().parents[2] / "outputs"


def result_folders(root: Path | None = None) -> list[Path]:
    root = root or outputs_root()
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir()
                  if p.is_dir() and (p / "index.json").exists())


def load(folder: Path) -> dict:
    index = json.loads((folder / "index.json").read_text())
    summary = json.loads((folder / "summary.json").read_text())
    return {"index": index, "summary": summary}


def get_path(obj, dotpath: str):
    """Resolve a dotted path (with optional [n] list indices) in a JSON
    object, e.g. expenses.tokens.total."""
    cur = obj
    for part in dotpath.split("."):
        idx = None
        if "[" in part:
            name, rest = part.split("[", 1)
            idx = int(rest.rstrip("]"))
            part = name
        if isinstance(cur, dict) and part:
            cur = cur.get(part)
        elif part == "":
            pass
        else:
            return None
        if idx is not None:
            try:
                cur = cur[idx]
            except (TypeError, IndexError, KeyError):
                return None
    return cur


def row_for(folder: Path, summary: dict) -> dict:
    md = summary.get("metadata", {})
    try:
        if (folder / "metadata.json").exists():
            md = json.loads((folder / "metadata.json").read_text())
    except (OSError, json.JSONDecodeError):
        pass
    ex = summary.get("expenses", {})
    c = summary.get("contestant", {})
    me = summary.get("measurements", {})
    ws = md.get("workspace", {})
    snap = summary.get("snapshot", {})
    agent_scores = None
    sessions = None
    run_identity = None
    env_snapshot = None
    try:
        if (folder / "agent_scores.json").exists():
            agent_scores = json.loads((folder / "agent_scores.json").read_text())
    except (OSError, json.JSONDecodeError):
        pass
    try:
        if (folder / "run_identity.json").exists():
            run_identity = json.loads((folder / "run_identity.json").read_text())
    except (OSError, json.JSONDecodeError):
        pass
    try:
        if (folder / "env_snapshot.json").exists():
            env_snapshot = json.loads((folder / "env_snapshot.json").read_text())
    except (OSError, json.JSONDecodeError):
        pass
    try:
        if (folder / "sessions.json").exists():
            sessions = json.loads((folder / "sessions.json").read_text())
    except (OSError, json.JSONDecodeError):
        pass
    an = (sessions or {}).get("analysis", {})
    ws_ = an.get("whole_session_stats", {})
    selected_roots = ((agent_scores or {}).get("session_selection") or {}).get("roots") or []
    primary_thread = (md.get("threads") or {}).get(selected_roots[0], {}) if selected_roots else {}
    efforts = primary_thread.get("reasoning_effort") or []
    if isinstance(efforts, str):
        efforts = [efforts]
    primary_model = primary_thread.get("entry_model") or primary_thread.get("model")
    primary_effort = primary_thread.get("entry_reasoning_effort") or (efforts[0] if efforts else None)
    primary_model_effort = " ".join(
        x for x in (primary_model, primary_effort) if x
    ) or None
    session_tokens = ws_.get("tokens") or {}
    agent_reviewed = bool(
        (agent_scores or {}).get("rubric", {}).get("total_scored") is not None
        or any(
            (agent_scores or {}).get("scores", {}).get(area, {}).get("overall_score") is not None
            for area in ("code_review", "cfd_review", "result_review")
        )
    )
    wall = (ex.get("time_seconds") or {}).get("wall_time")
    if not wall and an.get("window", {}).get("start") and an.get("window", {}).get("end"):
        from datetime import datetime
        try:
            wall = round(
                (datetime.fromisoformat(an["window"]["end"])
                 - datetime.fromisoformat(an["window"]["start"])).total_seconds(), 1)
        except (TypeError, ValueError):
            pass
    env_phase = (env_snapshot or {}).get("capture_phase")
    if env_phase is None and env_snapshot:
        provenance = env_snapshot.get("provenance") or {}
        if provenance.get("pre_run_authority") is False or "post-run" in str(
            provenance.get("capture_kind", "")
        ).lower():
            env_phase = "post_run"
        else:
            env_phase = "pre_run"
    return {
        "contestant": folder.name,
        "run_id": (run_identity or {}).get("run_id") or folder.name,
        "harness": md.get("harness", {}).get("harness"),
        "primary_model": primary_model,
        "primary_effort": primary_effort,
        "primary_model_effort": primary_model_effort,
        "status": md.get("status"),
        "goal_time_s": (ex.get("time_seconds") or {}).get("goal_time"),
        "wall_time_s": wall,
        "activity_time_s": (ex.get("time_seconds") or {}).get("activity_time_seconds"),
        "tokens": session_tokens.get("total", (ex.get("tokens") or {}).get("total")),
        "input_tokens": session_tokens.get("input"),
        "cached_input_tokens": session_tokens.get("cached_input"),
        "output_tokens": session_tokens.get("output"),
        "cost_usd": (ex.get("cost_estimate_usd") or {}).get("total"),
        "subagents": len(md.get("subagents", [])),
        "loc_lines": ((me.get("loc") or {}).get("file") or {}).get("lines"),
        "code_score": ((agent_scores or {}).get("scores", {}).get("code_review", {}) or {}).get(
            "overall_score", (summary.get("code_review") or {}).get("overall_score")),
        "cfd_score": ((agent_scores or {}).get("scores", {}).get("cfd_review", {}) or {}).get(
            "overall_score", (summary.get("cfd_review") or {}).get("overall_score")),
        "result_score": ((agent_scores or {}).get("scores", {}).get("result_review", {}) or {}).get(
            "overall_score", (summary.get("result_review") or {}).get("overall_score")),
        "rubric_total": ((agent_scores or {}).get("rubric") or {}).get("total_scored"),
        "execution_date": ((agent_scores or {}).get("session_selection") or {}).get("execution_date"),
        "disqualified": ((agent_scores or {}).get("disqualification") or {}).get("triggered"),
        "cache_hit": ws_.get("cache", {}).get("hit_ratio"),
        "session_buckets": len(an.get("buckets", [])),
        "env_captured": snap.get("env_snapshot_captured"),
        "env_capture_phase": env_phase,
        "agent_reviewed": agent_reviewed,
        "agents_md_sha": (ws.get("agents_md") or {}).get("sha256"),
        "codegraph": (ws.get("codegraph") or {}).get("exists"),
        "submodule": ((ws.get("benchmark_submodule") or {}).get("commit") or "?")[:12],
        "branch": c.get("branch"),
    }


def query_cli(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    ap = argparse.ArgumentParser(prog="cfdeval query",
                                 description="Query standardized evaluation result folders")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="list result folders")
    p_table = sub.add_parser("table", help="comparison table across contestants")
    p_table.add_argument("--json", action="store_true")
    p_show = sub.add_parser("show", help="print summary.md of a contestant")
    p_show.add_argument("contestant")
    p_get = sub.add_parser("get", help="extract a dotted JSON path from summary.json")
    p_get.add_argument("contestant")
    p_get.add_argument("dotpath")
    p_check = sub.add_parser("check", help="format-check a result folder")
    p_check.add_argument("folder")
    args = ap.parse_args(argv)
    folders = result_folders()

    if args.cmd == "list":
        for f in folders:
            r = load(f)
            s = r["summary"]
            print(f"{f.name:32s} {s.get('metadata', {}).get('harness', {}).get('harness', '?'):10s} "
                  f"{s.get('metadata', {}).get('status', '?')}")
        return 0

    if args.cmd == "table":
        rows = [row_for(f, load(f)["summary"]) for f in folders]
        if args.json:
            print(json.dumps(rows, indent=2))
            return 0
        headers = list(rows[0].keys()) if rows else []
        if not headers:
            print("no result folders found")
            return 0
        widths = {h: max(len(h), max((len(str(r[h])) for r in rows), default=0))
                  for h in headers}
        print(" | ".join(h.ljust(widths[h]) for h in headers))
        print("-+-".join("-" * widths[h] for h in headers))
        for r in rows:
            print(" | ".join(str(r[h]).ljust(widths[h]) for h in headers))
        return 0

    if args.cmd == "show":
        folder = outputs_root() / args.contestant
        if not (folder / "summary.md").exists():
            print(f"no result folder {args.contestant}", file=sys.stderr)
            return 1
        print((folder / "summary.md").read_text())
        return 0

    if args.cmd == "get":
        folder = outputs_root() / args.contestant
        if not (folder / "summary.json").exists():
            print(f"no result folder {args.contestant}", file=sys.stderr)
            return 1
        summary = json.loads((folder / "summary.json").read_text())
        value = get_path(summary, args.dotpath)
        if value is None and args.dotpath not in ("contestant",):
            pass
        print(json.dumps(value, indent=2) if isinstance(value, (dict, list)) else value)
        return 0

    if args.cmd == "check":
        from cfdeval import validation
        return validation.check_cli([args.folder])
    return 2

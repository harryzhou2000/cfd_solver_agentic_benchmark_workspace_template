"""cfdeval query — list, show, compare, and dot-path-query standardized
evaluation result folders."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path


CANONICAL_RUN_ID_RE = re.compile(r"^.+_[0-9a-f]{6}$")
COST_METADATA = Path(__file__).resolve().parents[2] / "config" / "cost_metadata.json"

CASE_SCORE_COLUMNS = {
    "case_m015_inv": "naca0012_m015_inviscid",
    "case_m080_inv": "naca0012_m080_inviscid",
    "case_m200_inv": "naca0012_m200_inviscid",
    "case_m015_re5k": "naca0012_m015_laminar_re5000",
    "case_m080_re5k": "naca0012_m080_laminar_re5000",
    "case_m200_re5k": "naca0012_m200_laminar_re5000",
    "case_cyl_re20": "cylinder_m010_laminar_re20",
    "case_cyl_re200": "cylinder_m010_laminar_re200",
}


def outputs_root() -> Path:
    return Path(__file__).resolve().parents[2] / "outputs"


def result_folders(root: Path | None = None) -> list[Path]:
    root = root or outputs_root()
    if not root.is_dir():
        return []
    return sorted(p for p in root.iterdir()
                  if (p.is_dir()
                      and CANONICAL_RUN_ID_RE.fullmatch(p.name)
                      and (p / "index.json").exists()))


def load(folder: Path) -> dict:
    index = json.loads((folder / "index.json").read_text())
    summary = json.loads((folder / "summary.json").read_text())
    return {"index": index, "summary": summary}


def current_cost_estimate(expenses: dict) -> dict | None:
    """Reprice snapshot token facts against the manager's current price table."""
    by_model = (expenses.get("tokens") or {}).get("by_model") or {}
    if not by_model:
        return None
    try:
        raw = COST_METADATA.read_bytes()
        meta = json.loads(raw)
    except (OSError, json.JSONDecodeError):
        return None
    from cfdeval.expenses import estimate_by_model
    estimate = estimate_by_model(meta, by_model)
    estimate.update({
        "metadata": str(COST_METADATA),
        "metadata_sha256": hashlib.sha256(raw).hexdigest(),
        "dashboard_current": True,
    })
    return estimate


def current_snapshot_cost(expenses: dict, metadata: dict,
                          agent_scores: dict | None = None,
                          decomposition: dict | None = None) -> dict | None:
    """Reprice the attributable snapshot token facts with current metadata.

    Codex stores model token aggregates in ``expenses.json``. OpenCode's
    persisted DB cost may legitimately be zero even when token counters are
    present, so use the manually selected OpenCode session tree represented by
    the model decomposition instead of treating that persisted zero as a
    current-price estimate.
    """
    harness = (metadata.get("harness") or {}).get("harness")
    if harness != "opencode":
        return current_cost_estimate(expenses)
    decomposition = decomposition or current_model_decomposition(
        expenses, metadata, agent_scores)
    rows = decomposition.get("rows") or []
    total = decomposition.get("total_current_cost_usd")
    if not rows:
        return {
            "total": None,
            "by_model": {},
            "unpriced_tokens": 0,
            "estimate": False,
            "metadata": None,
            "metadata_sha256": None,
            "dashboard_current": False,
            "source": "selected OpenCode session-tree token facts unavailable",
        }
    if not decomposition.get("total_tokens"):
        persisted = decomposition.get("total_persisted_cost_usd")
        if persisted is None:
            return None
        return {
            "total": persisted,
            "by_model": {},
            "unpriced_tokens": 0,
            "estimate": False,
            "metadata": None,
            "metadata_sha256": None,
            "dashboard_current": False,
            "source": "selected OpenCode session-tree persisted cost; token facts unavailable",
        }
    return {
        "total": total,
        "by_model": {
            row["key"]: {
                "usd": row.get("current_cost_usd"),
                "tokens": row.get("total", 0),
                "pricing": row.get("pricing"),
            }
            for row in rows
        },
        "unpriced_tokens": sum(
            int(row.get("total", 0) or 0)
            for row in rows if row.get("pricing") == "unresolved"
        ),
        "estimate": total is not None,
        "metadata": decomposition.get("cost_metadata"),
        "metadata_sha256": decomposition.get("cost_metadata_sha256"),
        "dashboard_current": True,
        "source": (
            "selected OpenCode session-tree token decomposition"
            if total is not None
            else "selected OpenCode session-tree token decomposition; one or more model prices unresolved"
        ),
    }


def environment_capture_phase(env_snapshot: dict | None) -> str | None:
    """Classify capture timing without rewriting explicit provenance.

    The explicit modern field is authoritative. Older snapshots defaulted to
    pre-run capture and may lack that field; legacy reconstruction markers are
    the only reason to classify those old documents as post-run. A Docker
    ``/opt`` external symlink describes runtime layout, not capture timing.
    """
    if not env_snapshot:
        return None
    phase = env_snapshot.get("capture_phase")
    if phase in ("pre_run", "post_run"):
        return phase
    provenance = env_snapshot.get("provenance") or {}
    if (provenance.get("pre_run_authority") is False
            or "post-run" in str(provenance.get("capture_kind", "")).lower()):
        return "post_run"
    # Version 1.0 predates capture_phase and was emitted only by setup-time
    # capture. Modern 1.1 documents must carry the explicit field; silently
    # treating a malformed one as authoritative pre-run evidence is unsafe.
    if env_snapshot.get("version") == "1.0":
        return "pre_run"
    return None


def current_model_decomposition(expenses: dict, metadata: dict,
                                agent_scores: dict | None = None) -> dict:
    """Build readable model + reasoning rows from immutable snapshot facts.

    Codex token facts are attributable per thread, while reasoning changes are
    only persisted as the set of settings seen. A multi-setting thread is
    therefore grouped under an explicit ``mixed(...)`` key rather than falsely
    assigning all tokens to its entry setting. OpenCode persists exact
    model+variant aggregates. The attribution_basis exposes that distinction.
    """
    try:
        raw = COST_METADATA.read_bytes()
        price_meta = json.loads(raw)
    except (OSError, json.JSONDecodeError):
        raw, price_meta = b"", None

    buckets: dict[tuple[str, str], dict] = {}

    def add(model: str, effort: str, tokens: dict, *, provider=None,
            persisted_cost=None, basis: str, mixed=None,
            token_split_available: bool = True) -> None:
        key = (model or "unknown", effort or "unknown")
        row = buckets.setdefault(key, {
            "model": key[0], "reasoning": key[1], "provider": provider,
            "input": 0, "cached_input": 0, "cache_write": 0, "output": 0,
            "reasoning_output": 0, "total": 0,
            "persisted_cost_usd": 0.0, "has_persisted_cost": False,
            "attribution_basis": basis, "mixed_efforts_seen": set(),
            "reasoning_is_output_subset": harness == "codex",
            "token_split_available": token_split_available,
        })
        row["token_split_available"] = (
            row["token_split_available"] and token_split_available)
        row["provider"] = row["provider"] or provider
        row["input"] += int(tokens.get("input", 0) or 0)
        row["cached_input"] += int(
            tokens.get("cached_input", tokens.get("cached", 0)) or 0)
        row["cache_write"] += int(tokens.get("cache_write", 0) or 0)
        row["output"] += int(tokens.get("output", 0) or 0)
        row["reasoning_output"] += int(
            tokens.get("reasoning_output", tokens.get("reasoning", 0)) or 0)
        row["total"] += int(tokens.get("total", 0) or 0)
        if persisted_cost is not None:
            row["persisted_cost_usd"] += float(persisted_cost or 0)
            row["has_persisted_cost"] = True
        row["mixed_efforts_seen"].update(mixed or [])

    harness = (metadata.get("harness") or {}).get("harness")
    by_thread = ((expenses.get("tokens") or {}).get("by_thread") or {})
    threads = metadata.get("threads") or {}
    if harness == "codex" and by_thread:
        for thread_id, token_info in by_thread.items():
            thread = threads.get(thread_id) or {}
            efforts = thread.get("reasoning_effort") or []
            if isinstance(efforts, str):
                efforts = [efforts]
            effort = (efforts[0] if len(efforts) == 1 else
                      f'mixed({",".join(sorted(set(efforts)))})' if efforts else "unknown")
            basis = ("single observed thread setting" if len(efforts) == 1 else
                     "thread aggregate; per-setting token split unavailable")
            if token_info.get("source") == "threads_fallback":
                basis += "; total-only token fallback"
            for model, token_bundle in (token_info.get("tokens") or {}).items():
                add(model, effort, token_bundle,
                    provider=thread.get("model_provider"), basis=basis,
                    mixed=efforts if len(efforts) > 1 else [],
                    token_split_available=token_info.get("source") != "threads_fallback")
    elif harness == "opencode" and (metadata.get("opencode") or {}).get("sessions"):
        oc_sessions = metadata["opencode"]["sessions"]
        by_id = {s.get("session_id"): s for s in oc_sessions if s.get("session_id")}
        roots = ((agent_scores or {}).get("session_selection") or {}).get("roots") or (
            (metadata.get("provenance") or {}).get("selected_roots") or [])
        if not roots:
            candidates = [sid for sid, info in by_id.items()
                          if not info.get("parent_id")]
            roots = candidates if len(candidates) == 1 else []
        selected = set()
        for session_id in by_id if roots else ():
            current, seen = session_id, set()
            while current and current not in seen:
                seen.add(current)
                if current in roots:
                    selected.add(session_id)
                    break
                current = (by_id.get(current) or {}).get("parent_id")
        for session_id in sorted(selected):
            info = by_id[session_id]
            units = info.get("usage_by_model") or [{
                "model": info.get("model"), "provider": info.get("provider"),
                "variant": info.get("variant"),
                "tokens_input": info.get("tokens_input", 0),
                "tokens_cache_read": info.get("tokens_cache_read", 0),
                "tokens_cache_write": info.get("tokens_cache_write", 0),
                "tokens_output": info.get("tokens_output", 0),
                "tokens_reasoning": info.get("tokens_reasoning", 0),
                "cost": info.get("cost"),
            }]
            for unit in units:
                model = unit.get("model") or "unknown"
                effort = unit.get("variant") or "unknown"
                output = int(unit.get("tokens_output", 0) or 0)
                reasoning = int(unit.get("tokens_reasoning", 0) or 0)
                raw_input = int(unit.get("tokens_input", 0) or 0)
                cache_read = int(unit.get("tokens_cache_read", 0) or 0)
                cache_write = int(unit.get("tokens_cache_write", 0) or 0)
                input_t = raw_input + cache_read + cache_write
                add(model, effort, {
                    "input": input_t, "cached_input": cache_read,
                    "cache_write": cache_write, "output": output,
                    "reasoning_output": reasoning,
                    "total": input_t + output + reasoning,
                }, provider=unit.get("provider"), persisted_cost=unit.get("cost"),
                    basis=("exact OpenCode assistant-message model + variant aggregate"
                           if info.get("usage_by_model") else
                           "OpenCode session-row model + variant aggregate"))
    else:
        for model, token_bundle in ((expenses.get("tokens") or {}).get("by_model") or {}).items():
            model_info = (metadata.get("models") or {}).get(model) or {}
            efforts = model_info.get("reasoning_efforts_seen") or []
            effort = efforts[0] if len(efforts) == 1 else ("mixed" if efforts else "unknown")
            add(model, effort, token_bundle, basis="model aggregate; no per-thread split",
                mixed=efforts if len(efforts) > 1 else [])

    rows = []
    total_current = 0.0
    total_persisted = 0.0
    unpriced_tokens = 0
    for (_model, _effort), row in sorted(buckets.items()):
        current_cost = None
        pricing = "unavailable"
        if price_meta is not None:
            from cfdeval.expenses import cost_for, model_cost
            candidates = []
            if row["provider"]:
                candidates.append(f'{row["provider"]}/{row["model"]}')
            candidates.append(row["model"])
            # Routed OpenCode model ids may embed deployment prefixes, e.g.
            # ``us/azure/openai/eccn-gpt-5.6-sol``. Prefer an unambiguous
            # canonical manager price key whose name is the routed suffix.
            configured = (price_meta.get("models") or {})
            suffix_aliases = [
                key for key in configured
                if "/" not in key and str(row["model"]).lower().endswith(key.lower())
            ]
            if len(suffix_aliases) == 1:
                candidates.insert(0, suffix_aliases[0])
            price_key = next((c for c in candidates
                              if c.lower() in configured), candidates[-1])
            price, defaults = model_cost(price_meta, price_key)
            if price is None:
                current_cost = None
                pricing = "unresolved"
                unpriced_tokens += row["total"]
            elif row["token_split_available"]:
                current_cost = round(cost_for(
                    price, input_t=row["input"], cached_t=row["cached_input"],
                    cache_write_t=row["cache_write"],
                    output_t=(row["output"] if row["reasoning_is_output_subset"]
                              else row["output"] + row["reasoning_output"])), 4)
                pricing = "metadata"
            else:
                current_cost = round(cost_for(price, total_t=row["total"]), 4)
                pricing = "metadata"
            if current_cost is not None:
                total_current += current_cost
        persisted = (round(row["persisted_cost_usd"], 6)
                     if row.pop("has_persisted_cost") else None)
        if persisted is not None:
            total_persisted += persisted
        mixed = sorted(row.pop("mixed_efforts_seen"))
        row.pop("reasoning_is_output_subset")
        split_available = row.pop("token_split_available")
        row.update({
            "key": f'{row["model"]} + {row["reasoning"]}',
            "non_cached_input": (max(
                row["input"] - row["cached_input"] - row["cache_write"], 0)
                                 if split_available else None),
            "current_cost_usd": current_cost,
            "persisted_cost_usd": persisted,
            "pricing": pricing,
            "mixed_efforts_seen": mixed,
            "token_split_available": split_available,
        })
        if not split_available:
            for field in ("input", "cached_input", "cache_write", "output", "reasoning_output"):
                row[field] = None
        rows.append(row)
    total_tokens = sum(r["total"] for r in rows)
    for row in rows:
        row["token_share"] = row["total"] / total_tokens if total_tokens else None
        row["current_cost_share"] = (
            row["current_cost_usd"] / total_current
            if row["current_cost_usd"] is not None and total_current else None)
        row["persisted_cost_share"] = (
            row["persisted_cost_usd"] / total_persisted
            if row["persisted_cost_usd"] is not None and total_persisted else None)
    return {
        "rows": rows,
        "total_tokens": total_tokens,
        "total_current_cost_usd": (
            round(total_current, 4)
            if price_meta is not None and not unpriced_tokens else None),
        "total_persisted_cost_usd": round(total_persisted, 6)
            if any(r["persisted_cost_usd"] is not None for r in rows) else None,
        "cost_metadata": str(COST_METADATA) if price_meta is not None else None,
        "cost_metadata_sha256": hashlib.sha256(raw).hexdigest() if raw else None,
        "limitations": [],
    }


def effective_metadata_status(metadata: dict) -> str | None:
    """Do not surface a stale prompt status after all questions were resolved."""
    status = metadata.get("status")
    questions = metadata.get("questions")
    if status == "needs_user_input" and isinstance(questions, list):
        unanswered = [q for q in questions if not q.get("answer")]
        if not unanswered:
            return "complete"
    return status


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
    opencode_sessions = (md.get("opencode") or {}).get("sessions") or []
    opencode_by_id = {s.get("session_id"): s for s in opencode_sessions if s.get("session_id")}
    primary_opencode = opencode_by_id.get(selected_roots[0], {}) if selected_roots else {}
    efforts = primary_thread.get("reasoning_effort") or []
    if isinstance(efforts, str):
        efforts = [efforts]
    primary_model = (primary_thread.get("entry_model") or primary_thread.get("model")
                     or primary_opencode.get("entry_model")
                     or primary_opencode.get("model"))
    primary_effort = (primary_thread.get("entry_reasoning_effort")
                      or (efforts[0] if efforts else None)
                      or primary_opencode.get("entry_variant")
                      or primary_opencode.get("variant"))
    primary_model_effort = " ".join(
        x for x in (primary_model, primary_effort) if x
    ) or None
    session_tokens = ws_.get("tokens") or {}
    decomposition = current_model_decomposition(ex, md, agent_scores)
    dashboard_cost = current_snapshot_cost(
        ex, md, agent_scores, decomposition=decomposition)
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
    env_phase = environment_capture_phase(env_snapshot)
    row = {
        "contestant": folder.name,
        "run_id": (run_identity or {}).get("run_id") or folder.name,
        "harness": md.get("harness", {}).get("harness"),
        "primary_model": primary_model,
        "primary_effort": primary_effort,
        "primary_model_effort": primary_model_effort,
        "status": effective_metadata_status(md),
        "goal_time_s": (ex.get("time_seconds") or {}).get("goal_time"),
        "wall_time_s": wall,
        "activity_time_s": (ex.get("time_seconds") or {}).get("activity_time_seconds"),
        "tokens": session_tokens.get("total", (ex.get("tokens") or {}).get("total")),
        "input_tokens": session_tokens.get("input"),
        "cached_input_tokens": session_tokens.get("cached_input"),
        "output_tokens": session_tokens.get("output"),
        "cost_usd": (dashboard_cost or ex.get("cost_estimate_usd") or {}).get("total"),
        "cost_metadata_sha256": (dashboard_cost or {}).get("metadata_sha256"),
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
    case_scores = (agent_scores or {}).get("case_scores") or {}
    row.update({
        column: (case_scores.get(case_id) or {}).get("score")
        for column, case_id in CASE_SCORE_COLUMNS.items()
    })
    return row


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

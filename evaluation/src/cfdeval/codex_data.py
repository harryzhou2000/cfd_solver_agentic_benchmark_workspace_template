"""Shared access to Codex telemetry stored in a workspace ``.sessions`` bundle."""

from __future__ import annotations

import json
import re
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


def project_paths(workspace: str | Path) -> dict[str, Path]:
    """Return the only telemetry/config paths permitted during evaluation."""
    root = Path(workspace).resolve() / ".sessions"
    codex = root / "codex"
    return {
        "sessions_bundle": root,
        "codex_root": codex,
        "state_db": codex / "state_5.sqlite",
        "goals_db": codex / "goals_1.sqlite",
        "logs_db": codex / "logs_2.sqlite",
        "sessions_root": codex / "sessions",
        "history": codex / "history.jsonl",
        "ocx_config": root / "opencodex" / "config.json",
        "ocx_catalog": codex / "opencodex-catalog.json",
        "plugins_root": codex / "plugins",
        "opencode_db": root / "opencode-data" / "opencode" / "opencode.db",
        "opencode_config_dir": root / "opencode-config",
        "opencodex_config_dir": root / "opencodex",
    }


def require_project_path(workspace: str | Path, path: str | Path,
                         label: str) -> Path:
    """Reject any evaluator telemetry/config input outside ``.sessions``."""
    bundle = (Path(workspace).resolve() / ".sessions").resolve()
    candidate = Path(path).expanduser().resolve()
    try:
        candidate.relative_to(bundle)
    except ValueError as exc:
        raise ValueError(
            f"{label} must be inside the contestant workspace .sessions bundle: "
            f"{candidate} is outside {bundle}"
        ) from exc
    return candidate


def local_telemetry_paths(workspace: str | Path, **overrides) -> dict[str, Path]:
    """Resolve optional CLI overrides while enforcing the project boundary."""
    paths = project_paths(workspace)
    for key, value in overrides.items():
        if value is not None:
            paths[key] = require_project_path(workspace, value, key.replace("_", "-"))
    return paths


def rebase_rollout_paths(threads: dict[str, dict], sessions_root: str | Path) -> None:
    """Point DB rollout references at immutable bundled files, never host paths.

    Migrated DB rows commonly retain an original ``~/.codex/sessions`` path.
    Match by rollout filename within the local bundle and set an absent or
    ambiguous match to ``None`` so callers fail closed.
    """
    root = Path(sessions_root)
    by_name: dict[str, list[Path]] = {}
    if root.is_dir():
        for path in root.rglob("rollout-*.jsonl"):
            if path.is_file():
                by_name.setdefault(path.name, []).append(path.resolve())
    all_rollouts = [p for matches in by_name.values() for p in matches]
    for thread_id, thread in threads.items():
        raw = thread.get("rollout_path")
        name = Path(raw).name if raw else ""
        matches = by_name.get(name, [])
        if not matches:
            matches = [p for p in all_rollouts if thread_id in p.name]
        thread["rollout_path"] = str(matches[0]) if len(matches) == 1 else None


def _ro_connect(path):
    return sqlite3.connect(f"file:{path}?mode=ro", uri=True)


def load_threads(state_db) -> dict[str, dict]:
    """thread_id -> row dict (cwd, model, tokens_used, rollout_path, timestamps)."""
    db = _ro_connect(state_db)
    try:
        rows = db.execute(
            "SELECT id, rollout_path, created_at, updated_at, cwd, model, "
            "tokens_used, git_branch, title, model_provider, agent_nickname, "
            "agent_role, agent_path, thread_source FROM threads"
        ).fetchall()
    finally:
        db.close()
    out = {}
    for r in rows:
        out[r[0]] = {
            "id": r[0],
            "rollout_path": r[1],
            "created_at": r[2],
            "updated_at": r[3],
            "cwd": r[4],
            "model": r[5],
            "tokens_used": r[6] or 0,
            "git_branch": r[7],
            "title": r[8],
            "model_provider": r[9],
            "agent_nickname": r[10],
            "agent_role": r[11],
            "agent_path": r[12],
            "thread_source": r[13],
        }
    return out


def load_spawn_edges(state_db) -> list[tuple[str, str]]:
    db = _ro_connect(state_db)
    try:
        rows = db.execute(
            "SELECT parent_thread_id, child_thread_id FROM thread_spawn_edges"
        ).fetchall()
    finally:
        db.close()
    return [(p, c) for p, c in rows]


def load_goals(goals_db) -> dict[str, dict]:
    db = _ro_connect(goals_db)
    try:
        rows = db.execute(
            "SELECT thread_id, objective, status, token_budget, tokens_used, "
            "time_used_seconds, created_at_ms, updated_at_ms FROM thread_goals"
        ).fetchall()
    finally:
        db.close()
    return {
        r[0]: {
            "objective": r[1],
            "status": r[2],
            "token_budget": r[3],
            "tokens_used": r[4] or 0,
            "time_used_seconds": r[5] or 0,
            "created_at_ms": r[6],
            "updated_at_ms": r[7],
        }
        for r in rows
    }


TOKEN_FIELDS = [
    "input_tokens",
    "cached_input_tokens",
    "non_cached_input_tokens",
    "output_tokens",
    "reasoning_output_tokens",
    "total_tokens",
]


def parse_turn_usage(body: str) -> dict | None:
    """Extract one turn's usage record from a structured log line."""
    if "codex.turn.token_usage" not in body:
        return None
    rec = {}
    for f in TOKEN_FIELDS:
        m = re.search(rf"codex\.turn\.token_usage\.{f}=(\d+)", body)
        rec[f] = int(m.group(1)) if m else 0
    if not any(rec[f] for f in TOKEN_FIELDS):
        return None
    m = re.search(r"turn\.id=([0-9a-fA-F-]+)", body)
    rec["turn_id"] = m.group(1) if m else None
    m = re.search(r"turn\{[^}]*model=([^\s}]+)", body)
    rec["model"] = m.group(1) if m else None
    m = re.search(r"codex\.turn\.reasoning_effort=([^\s}]+)", body)
    rec["reasoning_effort"] = m.group(1) if m else None
    return rec


def load_turn_usage(logs_db, thread_ids: set[str]) -> dict[str, list[dict]]:
    """thread_id -> list of per-turn usage records (grouped and summed by
    (turn_id, model); multiple records per turn are treated as separate
    submissions and summed)."""
    if not thread_ids:
        return {}
    db = _ro_connect(logs_db)
    try:
        q = ",".join("?" * len(thread_ids))
        rows = db.execute(
            f"SELECT thread_id, feedback_log_body FROM logs "
            f"WHERE thread_id IN ({q}) AND feedback_log_body LIKE '%token_usage%'",
            sorted(thread_ids),
        ).fetchall()
    finally:
        db.close()
    per_thread: dict[str, dict[tuple, dict]] = {}
    for tid, body in rows:
        if not body:
            continue
        rec = parse_turn_usage(body)
        if not rec:
            continue
        key = (rec["turn_id"], rec["model"])
        bucket = per_thread.setdefault(tid, {})
        agg = bucket.setdefault(key, {"model": rec["model"], **{f: 0 for f in TOKEN_FIELDS}})
        for f in TOKEN_FIELDS:
            agg[f] += rec[f]
    return {tid: list(buckets.values()) for tid, buckets in per_thread.items()}


def iter_session_records(rollout_path):
    """Yield parsed JSON records from a codex session rollout file."""
    if not rollout_path:
        return
    p = Path(rollout_path)
    if not p.exists():
        return
    with open(p, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def rollout_usage_facts(rollout_path: str) -> dict:
    """Latest cumulative usage, persisted context size, and prompt statistics."""
    final = {f: 0 for f in TOKEN_FIELDS}
    context_windows = []
    prompt_inputs = []
    for rec in iter_session_records(rollout_path):
        payload = rec.get("payload") or {}
        if rec.get("type") != "event_msg" or payload.get("type") != "token_count":
            continue
        info = payload.get("info") or {}
        total = info.get("total_token_usage") or {}
        if total and total.get("total_tokens", 0) >= final["total_tokens"]:
            final = {f: max(int(total.get(f, 0) or 0), 0) for f in TOKEN_FIELDS}
            final["non_cached_input_tokens"] = max(
                final["input_tokens"] - final["cached_input_tokens"], 0)
        cw = info.get("model_context_window")
        if isinstance(cw, int) and cw > 0:
            context_windows.append(cw)
        last_input = (info.get("last_token_usage") or {}).get("input_tokens")
        if isinstance(last_input, int) and last_input >= 0:
            prompt_inputs.append(last_input)
    return {
        **final,
        "model_context_window": max(context_windows) if context_windows else None,
        "max_prompt_input_tokens": max(prompt_inputs) if prompt_inputs else None,
        "mean_prompt_input_tokens": (
            sum(prompt_inputs) / len(prompt_inputs) if prompt_inputs else None
        ),
    }


def rollout_entry_settings(rollout_path: str) -> dict:
    """Model and effort applied when a Codex thread first entered the run."""
    for rec in iter_session_records(rollout_path):
        payload = rec.get("payload") or {}
        if rec.get("type") != "event_msg" or payload.get("type") != "thread_settings_applied":
            continue
        settings = payload.get("thread_settings") or {}
        return {
            "model": settings.get("model"),
            "reasoning_effort": settings.get("reasoning_effort"),
        }
    return {"model": None, "reasoning_effort": None}


def parse_iso(ts: str | None) -> datetime | None:
    if not ts:
        return None
    try:
        dt = datetime.fromisoformat(ts.replace("Z", "+00:00"))
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return dt
    except ValueError:
        return None


def session_window(rollout_path: str) -> tuple[datetime | None, datetime | None]:
    """Earliest/latest event timestamps in a rollout file."""
    start = end = None
    for rec in iter_session_records(rollout_path):
        dt = parse_iso(rec.get("timestamp"))
        if dt is None:
            continue
        if start is None or dt < start:
            start = dt
        if end is None or dt > end:
            end = dt
    return start, end


def select_threads(threads: dict[str, dict], workspace: str) -> dict[str, dict]:
    """Threads whose cwd is the workspace or nested inside it, plus all
    descendants (subagent trees) of those threads."""
    ws = str(Path(workspace).resolve())
    selected = {
        tid: t
        for tid, t in threads.items()
        if t["cwd"] and (Path(t["cwd"]).resolve() == Path(ws) or str(Path(t["cwd"]).resolve()).startswith(ws + os.sep))
    }
    return selected


def thread_trees(selected: dict[str, dict], edges: list[tuple[str, str]]) -> tuple[list[str], set[str]]:
    """Return (roots, all_thread_ids) where roots are selected threads that are
    not children of any selected thread, and all_thread_ids includes selected
    threads plus their descendants via spawn edges."""
    children = {c for _, c in edges}
    roots = [tid for tid in selected if tid not in children]
    all_ids = set(selected)
    children_map = {}
    for p, c in edges:
        if p in all_ids and c not in all_ids:
            children_map.setdefault(p, []).append(c)
    stack = list(all_ids)
    while stack:
        tid = stack.pop()
        for c in children_map.get(tid, []):
            if c not in all_ids:
                all_ids.add(c)
                stack.append(c)
    return roots, all_ids


def tree_of(root: str, threads: dict[str, dict], edges: list[tuple[str, str]]) -> set[str]:
    """All thread ids in the spawn tree rooted at `root` (root included)."""
    children = {}
    for p, c in edges:
        children.setdefault(p, []).append(c)
    ids = {root}
    stack = [root]
    while stack:
        tid = stack.pop()
        for c in children.get(tid, []):
            if c not in ids:
                ids.add(c)
                stack.append(c)
    return ids


def parse_roots(roots_arg: str | None) -> list[str] | None:
    """Parse a comma-separated --roots value; None means 'all roots'."""
    if not roots_arg:
        return None
    out = [r.strip() for r in roots_arg.split(",") if r.strip()]
    return out or None

"""Shared access to codex telemetry: threads, spawn edges, goals, usage logs,
and session rollouts. Stdlib only; all codex paths overridable so a snapshot
of another machine's ~/.codex can be used."""

from __future__ import annotations

import json
import os
import re
import sqlite3
from datetime import datetime, timezone
from pathlib import Path


def codex_home() -> Path:
    return Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))


def default_paths():
    home = codex_home()
    return {
        "state_db": home / "state_5.sqlite",
        "goals_db": home / "goals_1.sqlite",
        "logs_db": home / "logs_2.sqlite",
        "sessions_root": home / "sessions",
    }


def _ro_connect(path):
    return sqlite3.connect(f"file:{path}?mode=ro", uri=True)


def load_threads(state_db) -> dict[str, dict]:
    """thread_id -> row dict (cwd, model, tokens_used, rollout_path, timestamps)."""
    db = _ro_connect(state_db)
    try:
        rows = db.execute(
            "SELECT id, rollout_path, created_at, updated_at, cwd, model, "
            "tokens_used, git_branch, title FROM threads"
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

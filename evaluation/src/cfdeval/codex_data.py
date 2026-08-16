"""Shared access to Codex telemetry stored in a workspace ``.sessions`` bundle."""

from __future__ import annotations

import json
import os
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
    bundle = Path(workspace).resolve() / ".sessions"
    if bundle.is_symlink():
        raise ValueError(
            f"contestant workspace .sessions bundle must not be a symlink: {bundle}")
    paths = project_paths(workspace)
    for key, value in overrides.items():
        if value is not None:
            paths[key] = require_project_path(workspace, value, key.replace("_", "-"))
    return {
        key: require_project_path(workspace, value, key.replace("_", "-"))
        for key, value in paths.items()
    }


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


def _usage_fields(raw: dict | None) -> dict:
    raw = raw or {}
    return {field: max(int(raw.get(field, 0) or 0), 0)
            for field in TOKEN_FIELDS}


def rollout_task_started_ids(rollout_path: str | None) -> set[str]:
    """Return persisted task turn IDs from one bundled rollout."""
    return {
        str(turn_id)
        for rec in iter_session_records(rollout_path)
        if rec.get("type") == "event_msg"
        and (rec.get("payload") or {}).get("type") == "task_started"
        and (turn_id := (rec.get("payload") or {}).get("turn_id"))
    }


def rollout_ownership(rollout_path: str | None,
                      parent_rollout_path: str | None = None,
                      parent_task_ids: set[str] | None = None) -> dict:
    """Locate the non-inherited portion of a root or forked rollout.

    Forked Codex rollouts begin with their own ``session_meta`` and then replay
    the parent's persisted history.  Replay record timestamps are rewritten at
    fork time, so timestamps alone cannot identify ownership.  The first
    post-fork ``task_started`` is instead identified by both its creation-time
    epoch and a turn ID absent from the parent rollout.  Token counters before
    that boundary are the inherited cumulative baseline.
    """
    records = iter_session_records(rollout_path)
    first = next(records, None)
    zero = _usage_fields(None)
    if not first:
        return {
            "is_fork": False, "available": False, "boundary_turn_id": None,
            "baseline": zero, "inherited_records": 0,
            "reason": "rollout unavailable or empty",
        }
    meta = first.get("payload") or {}
    parent_id = meta.get("parent_thread_id") or meta.get("forked_from_id")
    if not parent_id:
        return {
            "is_fork": False, "available": True, "boundary_turn_id": None,
            "baseline": zero, "inherited_records": 0, "reason": None,
        }
    parent_available = (parent_task_ids is not None or (
        parent_rollout_path and Path(parent_rollout_path).is_file()))
    parent_turns = (parent_task_ids if parent_task_ids is not None else
                    rollout_task_started_ids(parent_rollout_path)
                    if parent_available else set())
    created = parse_iso(meta.get("timestamp"))
    created_second = int(created.timestamp()) if created else None
    baseline = zero
    inherited = 0
    for rec in records:
        payload = rec.get("payload") or {}
        if rec.get("type") == "event_msg" and payload.get("type") == "task_started":
            turn_id = payload.get("turn_id")
            started_at = payload.get("started_at")
            new_turn = (turn_id and str(turn_id) not in parent_turns
                        if parent_available else turn_id and inherited == 0)
            time_ok = (created_second is None or not isinstance(started_at, (int, float))
                       or int(started_at) >= created_second)
            if new_turn and time_ok:
                return {
                    "is_fork": True, "available": True,
                    "boundary_turn_id": str(turn_id), "baseline": baseline,
                    "inherited_records": inherited, "reason": None,
                    "boundary_basis": ("parent-turn-id-and-creation-epoch"
                                       if parent_available else
                                       "first-record-task-no-replay"),
                }
        if rec.get("type") == "event_msg" and payload.get("type") == "token_count":
            total = ((payload.get("info") or {}).get("total_token_usage") or {})
            if total:
                baseline = _usage_fields(total)
        inherited += 1
    return {
        "is_fork": True, "available": False, "boundary_turn_id": None,
        "baseline": baseline, "inherited_records": inherited,
        "reason": ("fork-owned task boundary not found"
                   if parent_available else
                   "fork parent rollout unavailable and replay boundary ambiguous"),
    }


def iter_owned_session_records(rollout_path: str | None,
                               ownership: dict):
    """Yield only records owned by this thread, plus its own session metadata."""
    boundary = ownership.get("boundary_turn_id")
    if not ownership.get("available"):
        return
    if not ownership.get("is_fork"):
        yield from iter_session_records(rollout_path)
        return
    started = False
    for index, rec in enumerate(iter_session_records(rollout_path)):
        if index == 0 and rec.get("type") == "session_meta":
            yield rec
            continue
        payload = rec.get("payload") or {}
        if (not started and rec.get("type") == "event_msg"
                and payload.get("type") == "task_started"
                and str(payload.get("turn_id")) == boundary):
            started = True
        if started:
            yield rec


def rollout_usage_facts(rollout_path: str,
                        parent_rollout_path: str | None = None,
                        parent_task_ids: set[str] | None = None,
                        fallback_model: str | None = None) -> dict:
    """Thread-owned usage, persisted context size, and prompt statistics."""
    ownership = rollout_ownership(
        rollout_path, parent_rollout_path, parent_task_ids)
    baseline = ownership["baseline"]
    previous = dict(baseline)
    owned = _usage_fields(None)
    usage_by_model: dict[str, dict] = {}
    current_model = fallback_model
    explicit_model_seen = False
    fallback_model_tokens = 0
    context_windows = []
    prompt_inputs = []
    for rec in iter_owned_session_records(rollout_path, ownership):
        payload = rec.get("payload") or {}
        observed_model = None
        if rec.get("type") == "turn_context":
            observed_model = payload.get("model")
        elif (rec.get("type") == "event_msg"
              and payload.get("type") == "thread_settings_applied"):
            observed_model = (payload.get("thread_settings") or {}).get("model")
        if observed_model:
            current_model = str(observed_model)
            explicit_model_seen = True
        if rec.get("type") != "event_msg" or payload.get("type") != "token_count":
            continue
        info = payload.get("info") or {}
        total = info.get("total_token_usage") or {}
        if total:
            cur = _usage_fields(total)
            if cur["total_tokens"] < previous["total_tokens"]:
                delta = cur
            else:
                delta = {field: max(cur[field] - previous[field], 0)
                         for field in TOKEN_FIELDS}
            for field in TOKEN_FIELDS:
                owned[field] += delta[field]
            model = current_model or "unknown"
            model_usage = usage_by_model.setdefault(model, _usage_fields(None))
            for field in TOKEN_FIELDS:
                model_usage[field] += delta[field]
            if not explicit_model_seen:
                fallback_model_tokens += delta["total_tokens"]
            previous = cur
        cw = info.get("model_context_window")
        if isinstance(cw, int) and cw > 0:
            context_windows.append(cw)
        last_input = (info.get("last_token_usage") or {}).get("input_tokens")
        if isinstance(last_input, int) and last_input >= 0:
            prompt_inputs.append(last_input)
    if not ownership.get("available"):
        owned = _usage_fields(None)
        usage_by_model = {}
        fallback_model_tokens = 0
    owned["non_cached_input_tokens"] = max(
        owned["input_tokens"] - owned["cached_input_tokens"], 0)
    for model_usage in usage_by_model.values():
        model_usage["non_cached_input_tokens"] = max(
            model_usage["input_tokens"] - model_usage["cached_input_tokens"], 0)
    return {
        **owned,
        "usage_by_model": usage_by_model,
        "fallback_model_tokens": fallback_model_tokens,
        "model_context_window": max(context_windows) if context_windows else None,
        "max_prompt_input_tokens": max(prompt_inputs) if prompt_inputs else None,
        "mean_prompt_input_tokens": (
            sum(prompt_inputs) / len(prompt_inputs) if prompt_inputs else None
        ),
        "accounting": ownership,
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
    descendants (subagent trees) of those threads.

    Docker-isolated runs record the mounted project as ``/workspace`` while
    the bundled database is later read from its host-side contestant repo.
    Since the database itself is constrained to that repo's ``.sessions``
    bundle, treat the container mount namespace as a candidate match too.
    """
    ws = str(Path(workspace).resolve())

    def matches(cwd: str | None) -> bool:
        if not cwd:
            return False
        if cwd == "/workspace" or cwd.startswith("/workspace/"):
            return True
        resolved = str(Path(cwd).resolve())
        return resolved == ws or resolved.startswith(ws + os.sep)

    selected = {
        tid: t
        for tid, t in threads.items()
        if matches(t["cwd"])
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


def select_opencode_session_trees(sessions: list[dict],
                                  roots: list[str] | None) -> list[dict]:
    """Restrict OpenCode candidate rows to requested roots and descendants."""
    if roots is None:
        return sessions
    by_id = {s.get("session_id"): s for s in sessions if s.get("session_id")}
    missing = [root for root in roots if root not in by_id]
    if missing:
        raise ValueError(f"unknown OpenCode root session ids: {missing}")
    requested = set(roots)
    selected = []
    for session in sessions:
        current = session.get("session_id")
        seen = set()
        while current and current not in seen:
            seen.add(current)
            if current in requested:
                selected.append(dict(session))
                break
            current = (by_id.get(current) or {}).get("parent_id")
    for session in selected:
        if session.get("session_id") in requested:
            session["original_parent_id"] = session.get("parent_id")
            session["parent_id"] = None
    return selected

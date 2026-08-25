"""Read Claude Code telemetry bundled in ``<workspace>/.sessions/claude``.

Claude Code persists newline-delimited JSON beneath ``projects/``.  This
module deliberately treats the files as an evidence format rather than a
stable Python API: fields are read defensively, malformed lines and ambiguous
session identifiers are reported, and no path outside the contestant bundle
is ever followed.
"""

from __future__ import annotations

import hashlib
import json
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

from cfdeval import codex_data as cd
from cfdeval import redact


def claude_root(workspace: str | Path, override: str | Path | None = None) -> Path:
    """Return the workspace-local Claude home and reject escape overrides."""
    ws = Path(workspace).resolve()
    raw = Path(override) if override is not None else cd.project_paths(ws)["claude_root"]
    if raw.is_symlink():
        raise ValueError(f"claude-root must not be a symlink: {raw}")
    return cd.require_project_path(ws, raw, "claude-root")


def parse_timestamp(value: Any) -> datetime | None:
    if isinstance(value, (int, float)):
        # Claude history has used both seconds and milliseconds.
        seconds = value / 1000 if value > 10_000_000_000 else value
        try:
            return datetime.fromtimestamp(seconds, tz=timezone.utc)
        except (OverflowError, OSError, ValueError):
            return None
    if not isinstance(value, str) or not value:
        return None
    text = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        dt = datetime.fromisoformat(text)
    except ValueError:
        return None
    return dt.replace(tzinfo=timezone.utc) if dt.tzinfo is None else dt.astimezone(timezone.utc)


def _jsonl(path: Path) -> tuple[list[dict], list[dict]]:
    rows: list[dict] = []
    errors: list[dict] = []
    try:
        lines = path.read_bytes().splitlines(keepends=True)
    except OSError as exc:
        return [], [{"path": str(path), "line": None, "error": str(exc)}]
    for number, raw_line in enumerate(lines, 1):
        if not raw_line.strip():
            continue
        try:
            row = json.loads(raw_line)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            errors.append({"path": str(path), "line": number,
                           "error": f"invalid JSON/UTF-8: {exc}"})
            continue
        if not isinstance(row, dict):
            errors.append({"path": str(path), "line": number,
                           "error": "record is not a JSON object"})
            continue
        row = dict(row)
        row["_line"] = number
        row["_raw_bytes"] = raw_line
        rows.append(row)
    return rows, errors


def _is_subagent_path(path: Path) -> bool:
    return "subagents" in path.parts


def _root_file_candidates(root: Path) -> list[Path]:
    projects = root / "projects"
    if projects.is_symlink():
        raise ValueError(f"Claude projects directory must not be a symlink: {projects}")
    if not projects.is_dir():
        return []
    return sorted(
        p for p in projects.rglob("*.jsonl")
        if not _is_subagent_path(p)
    )


def _first_value(rows: Iterable[dict], *keys: str) -> Any:
    for row in rows:
        for key in keys:
            value = row.get(key)
            if value not in (None, ""):
                return value
    return None


def _record_cwd(rows: Iterable[dict]) -> str | None:
    value = _first_value(rows, "cwd", "projectPath", "directory")
    return str(value) if value is not None else None


def cwd_matches(workspace: str | Path, cwd: str | None) -> bool:
    if not cwd:
        return False
    ws = str(Path(workspace).resolve())
    return (cwd == ws or cwd.startswith(ws + "/") or cwd == "/workspace"
            or cwd.startswith("/workspace/"))


def _root_id(path: Path, rows: list[dict]) -> str:
    value = _first_value(rows, "sessionId", "session_id")
    return str(value) if value else path.stem


def discover(workspace: str | Path, root_override: str | Path | None = None) -> dict:
    """Inventory root transcripts without deciding which one is primary."""
    root = claude_root(workspace, root_override)
    sessions = []
    errors = []
    ids: dict[str, list[str]] = {}
    for candidate in _root_file_candidates(root):
        if candidate.is_symlink():
            errors.append({"path": str(candidate), "line": None,
                           "error": "transcript symlinks are prohibited"})
            continue
        try:
            path = cd.require_project_path(workspace, candidate, "claude-transcript")
        except ValueError as exc:
            errors.append({"path": str(candidate), "line": None,
                           "error": str(exc)})
            continue
        rows, row_errors = _jsonl(path)
        errors.extend(row_errors)
        sid = _root_id(path, rows)
        ids.setdefault(sid, []).append(str(path))
        stamps = [parse_timestamp(r.get("timestamp") or r.get("ts")) for r in rows]
        stamps = [s for s in stamps if s is not None]
        cwd = _record_cwd(rows)
        subdir = path.with_suffix("") / "subagents"
        subagents = sorted(str(p) for p in subdir.rglob("*.jsonl")
                           if not p.is_symlink() and p.is_file())
        sessions.append({
            "session_id": sid,
            "path": str(path),
            "cwd": cwd,
            "workspace_match": cwd_matches(workspace, cwd),
            "started_at": min(stamps).isoformat() if stamps else None,
            "ended_at": max(stamps).isoformat() if stamps else None,
            "records": len(rows),
            "subagent_files": len(subagents),
        })
    duplicates = {sid: paths for sid, paths in ids.items() if len(paths) > 1}
    return {
        "root": str(root),
        "reachable": root.is_dir(),
        "sessions": sessions,
        "malformed_records": errors,
        "ambiguous_session_ids": duplicates,
    }


def _message(row: dict) -> dict:
    value = row.get("message")
    return value if isinstance(value, dict) else {}


def _role(row: dict) -> str | None:
    return _message(row).get("role") or row.get("type")


def _content(row: dict) -> list[dict]:
    value = _message(row).get("content")
    if isinstance(value, str):
        return [{"type": "text", "text": value}]
    if isinstance(value, list):
        return [v for v in value if isinstance(v, dict)]
    return []


def _usage(row: dict) -> dict:
    raw = _message(row).get("usage") or row.get("usage") or {}
    if not isinstance(raw, dict):
        raw = {}
    plain = max(int(raw.get("input_tokens", 0) or 0), 0)
    read = max(int(raw.get("cache_read_input_tokens", 0) or 0), 0)
    write = max(int(raw.get("cache_creation_input_tokens", 0) or 0), 0)
    output = max(int(raw.get("output_tokens", 0) or 0), 0)
    return {
        "input": plain + read + write,
        "cached": read,
        "cache_write": write,
        "non_cached": plain + write,
        "output": output,
        "reasoning": 0,
        "total": plain + read + write + output,
        "raw_input": plain,
    }


def _model(row: dict) -> str:
    return str(_message(row).get("model") or row.get("model") or "unknown")


def _subagent_id(path: Path, rows: list[dict]) -> str:
    value = _first_value(rows, "agentId", "agent_id")
    return str(value) if value else path.stem.removeprefix("agent-")


def _selected_files(root_file: Path) -> list[tuple[Path, bool]]:
    files: list[tuple[Path, bool]] = [(root_file, False)]
    session_dir = root_file.with_suffix("")
    subdir = session_dir / "subagents"
    if session_dir.is_symlink() or subdir.is_symlink():
        raise ValueError(f"Claude subagent directory must not be a symlink: {subdir}")
    for path in sorted(subdir.rglob("*.jsonl")) if subdir.is_dir() else []:
        files.append((path, True))
    return files


def select(workspace: str | Path, roots: list[str] | None,
           root_override: str | Path | None = None) -> dict:
    """Load explicitly selected root trees and their bundled subagents."""
    if not roots:
        raise ValueError("Claude extraction requires explicit --roots")
    disc = discover(workspace, root_override)
    selected_ambiguities = {
        sid: paths for sid, paths in disc["ambiguous_session_ids"].items()
        if sid in roots
    }
    if selected_ambiguities:
        raise ValueError(
            "ambiguous Claude session ids: "
            + ", ".join(sorted(selected_ambiguities)))
    by_id = {s["session_id"]: s for s in disc["sessions"]}
    missing = [root for root in roots if root not in by_id]
    if missing:
        raise ValueError(f"unknown Claude root session ids: {missing}")
    entities = []
    entity_ids: set[str] = set()
    errors = []
    for requested in roots:
        root_session = by_id[requested]
        root_file = Path(root_session["path"])
        pending_inline: dict[str, list[dict]] = {}
        for candidate, is_sub in _selected_files(root_file):
            # Re-assert containment after resolving every discovered file.
            if candidate.is_symlink():
                errors.append({"path": str(candidate), "line": None,
                               "error": "transcript symlinks are prohibited"})
                continue
            path = cd.require_project_path(workspace, candidate, "claude-transcript")
            rows, row_errors = _jsonl(path)
            errors.extend(row_errors)
            if not is_sub:
                for row in rows:
                    if row.get("isSidechain"):
                        agent_id = str(row.get("agentId") or row.get("agent_id")
                                       or "unknown")
                        pending_inline.setdefault(agent_id, []).append(row)
                rows = [row for row in rows if not row.get("isSidechain")]
            local_id = _subagent_id(path, rows) if is_sub else requested
            entity_id = f"{requested}:{local_id}" if is_sub else requested
            if entity_id in entity_ids:
                raise ValueError(
                    f"duplicate Claude entity id in selected tree: {entity_id}")
            entity_ids.add(entity_id)
            parent_id = requested if is_sub else None
            stamps = [parse_timestamp(r.get("timestamp") or r.get("ts")) for r in rows]
            stamps = [s for s in stamps if s is not None]
            entities.append({
                "entity_id": entity_id,
                "root_id": requested,
                "parent_id": parent_id,
                "is_subagent": is_sub,
                "path": str(path),
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                "bytes": path.stat().st_size,
                "cwd": _record_cwd(rows),
                "started_at": min(stamps).isoformat() if stamps else None,
                "ended_at": max(stamps).isoformat() if stamps else None,
                "records": rows,
                "inline": False,
            })
        persisted_uuids = {
            str(row.get("uuid"))
            for entity in entities
            if entity["root_id"] == requested and entity["is_subagent"]
            and not entity.get("inline")
            for row in entity["records"] if row.get("uuid")
        }
        for agent_id, inline_rows in sorted(pending_inline.items()):
            inline_rows = [row for row in inline_rows
                           if not row.get("uuid")
                           or str(row.get("uuid")) not in persisted_uuids]
            if not inline_rows:
                continue
            entity_id = f"{requested}:inline:{agent_id}"
            if entity_id in entity_ids:
                raise ValueError(
                    f"duplicate Claude entity id in selected tree: {entity_id}")
            entity_ids.add(entity_id)
            stamps = [parse_timestamp(r.get("timestamp") or r.get("ts"))
                      for r in inline_rows]
            stamps = [stamp for stamp in stamps if stamp is not None]
            entities.append({
                "entity_id": entity_id,
                "root_id": requested,
                "parent_id": requested,
                "is_subagent": True,
                "path": str(root_file),
                "sha256": hashlib.sha256(root_file.read_bytes()).hexdigest(),
                "bytes": root_file.stat().st_size,
                "cwd": _record_cwd(inline_rows),
                "started_at": min(stamps).isoformat() if stamps else None,
                "ended_at": max(stamps).isoformat() if stamps else None,
                "records": inline_rows,
                "inline": True,
            })
    if errors:
        first = errors[0]
        raise ValueError(
            f"selected Claude evidence contains {len(errors)} unreadable or "
            f"malformed record(s); first: {first.get('path')}:{first.get('line')} "
            f"{first.get('error')}")
    return {"root": disc["root"], "roots": list(roots), "entities": entities,
            "malformed_records": []}


def assistant_groups(rows: list[dict]) -> list[dict]:
    """Bundle persisted fragments of each assistant API message.

    Claude may write text and tool-use fragments with the same message ID as
    separate JSONL records. Usage is taken once from the richest fragment;
    content blocks are unioned in persisted order and deduplicated by stable
    block ID (or exact canonical content when no ID exists).
    """
    grouped: dict[str, list[dict]] = {}
    order: list[str] = []
    for row in rows:
        if _role(row) != "assistant":
            continue
        value = _message(row).get("id") or row.get("uuid")
        key = str(value) if value else f"__anonymous_line_{row.get('_line')}"
        if key not in grouped:
            grouped[key] = []
            order.append(key)
        grouped[key].append(row)

    out = []
    for key in order:
        fragments = grouped[key]
        usage_row = max(
            fragments,
            key=lambda row: (_usage(row)["total"], len(_content(row)),
                             int(row.get("_line", 0))),
        )
        blocks = []
        seen_blocks = set()
        for row in fragments:
            for block in _content(row):
                stable_id = block.get("id") or block.get("tool_use_id")
                block_key = ((block.get("type"), str(stable_id)) if stable_id else
                             (block.get("type"), json.dumps(
                                 block, sort_keys=True, separators=(",", ":"),
                                 ensure_ascii=False)))
                if block_key in seen_blocks:
                    continue
                seen_blocks.add(block_key)
                blocks.append({"block": block, "row": row})
        stamps = [parse_timestamp(row.get("timestamp") or row.get("ts"))
                  for row in fragments]
        stamps = [stamp for stamp in stamps if stamp is not None]
        efforts = []
        for row in fragments:
            effort = row.get("effort") or _message(row).get("effort")
            if effort and effort not in efforts:
                efforts.append(str(effort))
        terminal_rows = [
            row for row in fragments
            if (_message(row).get("stop_reason") or row.get("stop_reason")) == "end_turn"
            and any(block.get("type") == "text" for block in _content(row))
        ]
        terminal_row = max(
            terminal_rows,
            key=lambda row: (parse_timestamp(row.get("timestamp") or row.get("ts"))
                             or datetime.min.replace(tzinfo=timezone.utc),
                             len(_content(row)), int(row.get("_line", 0))),
        ) if terminal_rows else None
        out.append({
            "message_id": key,
            "rows": fragments,
            "usage_row": usage_row,
            "usage": _usage(usage_row),
            "model": _model(usage_row),
            "blocks": blocks,
            "timestamp": max(stamps) if stamps else None,
            "is_sidechain": any(bool(row.get("isSidechain")) for row in fragments),
            "efforts": efforts,
            "terminal_row": terminal_row,
        })
    return out


def facts(workspace: str | Path, roots: list[str],
          root_override: str | Path | None = None) -> dict:
    """Normalize selected Claude transcripts into evaluation facts."""
    selected = select(workspace, roots, root_override)
    all_events = []
    token_events = []
    tool_events = []
    untimed_tool_events = []
    turn_events = []
    untimed_usage = []
    by_model: dict[str, dict] = {}
    per_entity: dict[str, dict] = {}
    usage_by_entity: dict[str, dict[str, dict]] = {}
    usage_attribution_by_entity: dict[str, list[dict]] = {}
    prompts: dict[str, dict] = {}
    models_by_entity: dict[str, Counter] = {}
    efforts_by_entity: dict[str, list[str]] = {}
    efforts_by_model: dict[str, list[str]] = {}
    inline_sidechain_records_excluded = 0
    subagent_assistant_uuids = {
        str(row.get("uuid"))
        for entity in selected["entities"] if entity["is_subagent"]
        for row in entity["records"]
        if _role(row) == "assistant" and row.get("uuid")
    }

    for entity in selected["entities"]:
        eid = entity["entity_id"]
        tools = Counter()
        model_counts = Counter()
        entity_usage: dict[str, dict] = {}
        entity_attribution: dict[tuple[str, str], dict] = {}
        user_messages = []
        entity_total = 0
        seen_tool_ids: set[str] = set()
        entity_efforts: list[str] = []
        for row in entity["records"]:
            ts = parse_timestamp(row.get("timestamp") or row.get("ts"))
            role = _role(row)
            if role != "assistant" and ts is not None:
                all_events.append((ts, "claude", eid, "message", {"role": role}))
            if role == "user":
                texts = [b.get("text", "") for b in _content(row)
                         if b.get("type") == "text" and b.get("text")]
                if texts and not row.get("isMeta"):
                    user_messages.append({"timestamp": ts.isoformat() if ts else None,
                                          "text": "".join(texts)[:2000]})
                if ts is not None:
                    turn_events.append((ts, "user_message"))
        for group in assistant_groups(entity["records"]):
            duplicated_in_subagent_file = any(
                str(row.get("uuid")) in subagent_assistant_uuids
                for row in group["rows"] if row.get("uuid"))
            if (group["is_sidechain"] or duplicated_in_subagent_file) \
                    and not entity["is_subagent"]:
                inline_sidechain_records_excluded += len(group["rows"])
                continue
            ts = group["timestamp"]
            if ts is not None:
                all_events.append((ts, "claude", eid, "message", {"role": "assistant"}))
            usage = group["usage"]
            model = group["model"]
            effort_key = (group["efforts"][0] if len(group["efforts"]) == 1
                          else f"mixed({','.join(sorted(group['efforts']))})"
                          if group["efforts"] else "unknown")
            for effort in group["efforts"]:
                if effort not in entity_efforts:
                    entity_efforts.append(effort)
                model_efforts = efforts_by_model.setdefault(model, [])
                if effort not in model_efforts:
                    model_efforts.append(effort)
            tool_blocks = [item for item in group["blocks"]
                           if item["block"].get("type") == "tool_use"]
            if usage["total"]:
                if ts is not None:
                    token_events.append((ts, usage, eid))
                else:
                    untimed_usage.append((usage, eid, entity["path"],
                                          group["usage_row"].get("_line")))
                entity_total += usage["total"]
                model_counts[model] += usage["total"]
                agg = by_model.setdefault(model, {
                    "input": 0, "cached_input": 0, "cache_write": 0,
                    "non_cached_input": 0, "output": 0,
                    "reasoning_output": 0, "reasoning_is_output_subset": True,
                    "total": 0,
                })
                for src, dst in (("input", "input"), ("cached", "cached_input"),
                                 ("cache_write", "cache_write"),
                                 ("non_cached", "non_cached_input"),
                                 ("output", "output"), ("total", "total")):
                    agg[dst] += usage[src]
                eagg = entity_usage.setdefault(model, {
                    "input": 0, "cached_input": 0, "cache_write": 0,
                    "non_cached_input": 0, "output": 0,
                    "reasoning_output": 0, "reasoning_is_output_subset": True,
                    "total": 0,
                })
                for src, dst in (("input", "input"), ("cached", "cached_input"),
                                 ("cache_write", "cache_write"),
                                 ("non_cached", "non_cached_input"),
                                 ("output", "output"), ("total", "total")):
                    eagg[dst] += usage[src]
                aagg = entity_attribution.setdefault((model, effort_key), {
                    "model": model, "effort": effort_key,
                    "input": 0, "cached_input": 0, "cache_write": 0,
                    "non_cached_input": 0, "output": 0,
                    "reasoning_output": 0, "reasoning_is_output_subset": True,
                    "total": 0,
                })
                for src, dst in (("input", "input"), ("cached", "cached_input"),
                                 ("cache_write", "cache_write"),
                                 ("non_cached", "non_cached_input"),
                                 ("output", "output"), ("total", "total")):
                    aagg[dst] += usage[src]
            for item in tool_blocks:
                block = item["block"]
                block_row = item["row"]
                block_ts = parse_timestamp(
                    block_row.get("timestamp") or block_row.get("ts"))
                tool_id = block.get("id")
                if tool_id and str(tool_id) in seen_tool_ids:
                    continue
                if tool_id:
                    seen_tool_ids.add(str(tool_id))
                name = str(block.get("name") or "unknown")
                tools[name] += 1
                if block_ts is not None:
                    tool_events.append((block_ts, name, eid))
                    all_events.append((block_ts, "claude", eid, "tool_call",
                                       {"name": name}))
                else:
                    untimed_tool_events.append((name, eid, entity["path"],
                                                block_row.get("_line")))
            if ts is not None:
                turn_events.append((ts, "assistant_message"))
        models_by_entity[eid] = model_counts
        efforts_by_entity[eid] = entity_efforts
        usage_by_entity[eid] = entity_usage
        usage_attribution_by_entity[eid] = [
            entity_attribution[key] for key in sorted(entity_attribution)
        ]
        per_entity[eid] = {
            "kind": "claude_subagent" if entity["is_subagent"] else "claude_session",
            "is_subagent": entity["is_subagent"],
            "parent_id": entity["parent_id"],
            "root_id": entity["root_id"],
            "events": len(entity["records"]),
            "tokens_total": entity_total,
            "reasoning_efforts": entity_efforts,
            "tools": dict(tools),
            "first_event": entity["started_at"],
            "last_event": entity["ended_at"],
            "rollout_bytes": entity["bytes"],
            "source_sha256": entity["sha256"],
        }
        if not entity["is_subagent"]:
            prompts[entity["root_id"]] = {
                "initial_user_prompt": user_messages[0] if user_messages else None,
                "resume_prompts": user_messages[1:],
                "source": "claude_project_jsonl",
            }

    return {
        **selected,
        "all_events": all_events,
        "token_events": token_events,
        "tool_events": tool_events,
        "untimed_tool_events": untimed_tool_events,
        "turn_events": turn_events,
        "untimed_usage": untimed_usage,
        "by_model": by_model,
        "per_entity": per_entity,
        "models_by_entity": {k: dict(v) for k, v in models_by_entity.items()},
        "usage_by_entity": usage_by_entity,
        "usage_attribution_by_entity": usage_attribution_by_entity,
        "efforts_by_entity": efforts_by_entity,
        "efforts_by_model": efforts_by_model,
        "inline_sidechain_records_excluded": inline_sidechain_records_excluded,
        "prompts": prompts,
    }


def final_response(workspace: str | Path, root_id: str,
                   root_override: str | Path | None = None) -> dict:
    """Extract the last completed, user-visible root assistant response."""
    data = select(workspace, [root_id], root_override)
    root_entity = next(e for e in data["entities"] if not e["is_subagent"])
    subagent_uuids = {
        str(row.get("uuid")) for entity in data["entities"] if entity["is_subagent"]
        for row in entity["records"] if row.get("uuid")
    }
    candidates = []
    for group in assistant_groups(root_entity["records"]):
        duplicated_in_subagent_file = any(
            str(row.get("uuid")) in subagent_uuids
            for row in group["rows"] if row.get("uuid"))
        if (group["is_sidechain"] or duplicated_in_subagent_file
                or group["terminal_row"] is None):
            continue
        row = group["terminal_row"]
        text_items = [item for item in group["blocks"]
                      if item["block"].get("type") == "text"
                      and item["block"].get("text") is not None]
        texts = [str(item["block"].get("text")) for item in text_items]
        if texts:
            ts = parse_timestamp(row.get("timestamp") or row.get("ts"))
            if ts is not None:
                candidates.append((ts, group, row, texts, text_items))
    if not candidates:
        return {
            "status": "absent",
            "harness": "claude",
            "root_id": root_id,
            "source": root_entity["path"],
            "limitations": ["no assistant text record with stop_reason=end_turn"],
        }
    ordered = sorted(candidates, key=lambda item: item[0])
    if len(ordered) > 1 and ordered[-1][0] == ordered[-2][0]:
        return {
            "status": "ambiguous",
            "harness": "claude",
            "root_id": root_id,
            "source": root_entity["path"],
            "limitations": [
                "multiple completed assistant text records share the terminal timestamp"
            ],
        }
    ts, group, row, texts, text_items = ordered[-1]
    original_text = "".join(texts)
    text, redaction_count = redact.redact_text(original_text)
    text = text.replace("sk-" + redact.MARK, "[REDACTED_CREDENTIAL]")
    text = text.replace(redact.MARK, "[REDACTED_CREDENTIAL]")
    source_rows = []
    seen_rows = set()
    for item in text_items:
        source_row = item["row"]
        line = source_row.get("_line")
        if line not in seen_rows:
            seen_rows.add(line)
            source_rows.append(source_row)
    raw = b"".join(source_row.get("_raw_bytes", b"")
                   for source_row in source_rows)
    return {
        "status": "complete",
        "harness": "claude",
        "root_id": root_id,
        "message_id": group["message_id"],
        "timestamp": ts.isoformat(),
        "source": root_entity["path"],
        "source_line": row.get("_line"),
        "source_lines": [source_row.get("_line") for source_row in source_rows],
        "stored_message_sha256": hashlib.sha256(raw).hexdigest(),
        "text_sha256": hashlib.sha256(text.encode()).hexdigest(),
        "part_count": len(texts),
        "text": text,
        "redacted": redaction_count > 0,
        "redaction_count": redaction_count,
        "limitations": [],
    }

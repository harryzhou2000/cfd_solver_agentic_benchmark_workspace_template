#!/usr/bin/env python3
"""Extract execution metadata for a codex contestant run: harness, models
(incl. subagents + reasoning effort), context window used, subagent
threads/types, opencodex router facts (non-vanilla runs), and prompt history.

Usage:
  python3 evaluation/tools/extract_metadata.py --workspace <contestant-workspace>
    [--state-db PATH] [--goals-db PATH] [--logs-db PATH] [--sessions-root PATH]
    [--history PATH] [--ocx-config PATH] [--ocx-catalog PATH]
    [--plugins-root PATH] [--roots IDS] [--out PATH]

Spec: evaluation/specs/metadata_spec.md
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

from cfdeval import codex_data as cd


VANILLA_MODELS = {
    "gpt-5.6-sol", "gpt-5.6-terra", "gpt-5.6-luna", "gpt-5.5", "gpt-5.4",
    "gpt-5.4-mini", "gpt-5.3-codex-spark", "gpt-oss-120b", "gpt-oss-20b",
    "codex-auto-review",
}
HARNESS_WRAPPER_MARKERS = ("<codex_internal_context", "<environment_context>",
                           "AGENTS.md instructions")


def _run(cmd: list[str], timeout: int = 15) -> str | None:
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout.strip() or None
    except (OSError, subprocess.TimeoutExpired):
        return None


def session_meta(rollout_path: str) -> dict | None:
    for rec in cd.iter_session_records(rollout_path):
        if rec.get("type") == "session_meta":
            return rec.get("payload") or {}
    return None


def thread_efforts(rollout_path: str, usage_recs: list[dict]) -> list[str]:
    """Reasoning efforts recorded for a thread (turn_context + usage logs)."""
    efforts = []
    for rec in cd.iter_session_records(rollout_path):
        p = rec.get("payload") or {}
        if rec.get("type") == "turn_context":
            cm = p.get("collaboration_mode") or {}
            e = p.get("reasoning_effort") or (cm.get("settings") or {}).get("reasoning_effort")
            if e and e not in efforts:
                efforts.append(e)
        elif rec.get("type") == "event_msg" and p.get("type") == "thread_settings_applied":
            e = (p.get("thread_settings") or {}).get("reasoning_effort")
            if e and e not in efforts:
                efforts.append(e)
    for rec in usage_recs:
        e = rec.get("reasoning_effort")
        if e and e not in efforts:
            efforts.append(e)
    return efforts


def load_catalog(path: str) -> dict[str, dict]:
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError):
        return {}
    out = {}
    for m in data.get("models", []):
        slug = m.get("slug")
        if slug:
            out[slug.lower()] = m
    return out


def load_plugins(root: str) -> list[dict]:
    out = []
    base = Path(root)
    if not base.is_dir():
        return out
    for source in sorted(p for p in base.iterdir() if p.is_dir()):
        for name_dir in sorted(p for p in source.iterdir() if p.is_dir()):
            for ver in sorted(p for p in name_dir.iterdir() if p.is_dir()):
                out.append({"source": source.name, "name": name_dir.name,
                            "version": ver.name})
    return out


def workspace_state(ws: Path) -> dict:
    """AGENTS.md contents, .codegraph presence, and the benchmark submodule
    pointer for a contestant workspace."""
    agents = {"path": str(ws / "AGENTS.md"), "exists": False,
              "sha256": None, "content": None, "matches_git_head": None}
    am = ws / "AGENTS.md"
    if am.is_file():
        try:
            content = am.read_text(encoding="utf-8", errors="replace")
        except OSError:
            content = None
        if content is not None:
            agents.update({
                "exists": True,
                "sha256": hashlib.sha256(content.encode("utf-8")).hexdigest(),
                "content": content,
            })
            head = _run(["git", "-C", str(ws), "show", "HEAD:AGENTS.md"])
            if head is not None:
                agents["matches_git_head"] = (head.rstrip("\n") == content.rstrip("\n"))
    codegraph = {"exists": (ws / ".codegraph").is_dir(),
                 "path": str(ws / ".codegraph") if (ws / ".codegraph").is_dir() else None}
    bm_path = ws / "cfd_solver_agentic_benchmark"
    bm = {"path": str(bm_path), "exists": bm_path.is_dir(),
          "commit": None, "branch": None, "dirty": None, "origin_url": None}
    if bm["exists"]:
        bm["commit"] = _run(["git", "-C", str(bm_path), "rev-parse", "HEAD"])
        bm["branch"] = _run(["git", "-C", str(bm_path), "rev-parse", "--abbrev-ref", "HEAD"])
        dirty = _run(["git", "-C", str(bm_path), "status", "--porcelain"])
        bm["dirty"] = bool(dirty)
        bm["origin_url"] = _run(["git", "-C", str(bm_path), "config", "--get",
                                 "remote.origin.url"])
    return {
        "git": {
            "branch": _run(["git", "-C", str(ws), "rev-parse", "--abbrev-ref", "HEAD"]),
            "commit": _run(["git", "-C", str(ws), "rev-parse", "HEAD"]),
        },
        "agents_md": agents,
        "codegraph": codegraph,
        "benchmark_submodule": bm,
    }


def load_history(path: str) -> dict[str, list[dict]]:
    by_session: dict[str, list[dict]] = {}
    try:
        for line in Path(path).read_text().splitlines():
            row = json.loads(line)
            by_session.setdefault(row.get("session_id"), []).append(
                {"ts": row.get("ts"), "text": row.get("text", "")}
            )
    except (OSError, json.JSONDecodeError):
        pass
    return by_session


def clean_prompt(text: str) -> str:
    t = text.strip()
    if any(m in t for m in HARNESS_WRAPPER_MARKERS):
        return ""
    return t[:2000]


def fallback_prompts(rollout_path: str) -> list[dict]:
    """Non-harness user messages from the session, in order."""
    out = []
    for rec in cd.iter_session_records(rollout_path):
        p = rec.get("payload") or {}
        if rec.get("type") == "response_item" and p.get("type") == "message" \
                and p.get("role") == "user":
            text = ""
            for c in p.get("content") or []:
                if isinstance(c, dict) and c.get("type") == "input_text":
                    text += c.get("text", "")
            text = clean_prompt(text)
            if text:
                out.append({"timestamp": rec.get("timestamp"), "text": text})
    return out


def opencode_activity_times(db_path: str, session_ids: list[str],
                            idle_gap_seconds: int = 600) -> dict[str, dict]:
    """Per-session activity time from opencode message history.

    Each message carries `time.created` and (for assistant messages)
    `time.completed`, so consecutive events bracket actual work (turns and
    tool execution). Gaps between consecutive events longer than
    `idle_gap_seconds` are treated as interrupted idle time (user away / API
    stall) and excluded from activity time.
    """
    out: dict[str, dict] = {}
    try:
        db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return out
    try:
        for sid in session_ids:
            events = []
            for (data,) in db.execute(
                    "SELECT data FROM message WHERE session_id=?", (sid,)).fetchall():
                try:
                    d = json.loads(data)
                except json.JSONDecodeError:
                    continue
                t = d.get("time") or {}
                if t.get("created"):
                    events.append(t["created"])
                if t.get("completed"):
                    events.append(t["completed"])
            events = sorted(set(events))
            if len(events) < 2:
                out[sid] = {"activity_time_seconds": 0, "wall_time_seconds": 0,
                            "idle_time_seconds": 0, "idle_gaps": 0, "events": len(events)}
                continue
            wall = (events[-1] - events[0]) / 1000.0
            gaps = [(events[i + 1] - events[i]) / 1000.0
                    for i in range(len(events) - 1)]
            idle = [g for g in gaps if g > idle_gap_seconds]
            out[sid] = {
                "activity_time_seconds": round(wall - sum(idle), 1),
                "wall_time_seconds": round(wall, 1),
                "idle_time_seconds": round(sum(idle), 1),
                "idle_gaps": len(idle),
                "idle_gap_threshold_seconds": idle_gap_seconds,
                "events": len(events),
            }
    finally:
        db.close()
    return out


def opencode_message_usage(db_path: str, sessions: list[dict]) -> dict[str, list[dict]]:
    """Aggregate immutable OpenCode usage by the model on each assistant message.

    ``session.model`` is mutable and reflects the last model selected in a
    session. It therefore cannot attribute lifetime session counters when an
    agent switches models. Message rows retain the provider, model, variant,
    token categories, and provider-reported cost for each request.
    """
    by_session: dict[str, list[dict]] = {}
    fallback = {s["session_id"]: s for s in sessions}
    try:
        db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    except sqlite3.Error:
        return by_session
    try:
        for sid, session in fallback.items():
            aggregates: dict[tuple[str, str, str | None], dict] = {}
            try:
                rows = db.execute(
                    "SELECT id, data, time_created FROM message "
                    "WHERE session_id=? ORDER BY time_created, id", (sid,)
                ).fetchall()
            except sqlite3.Error:
                continue
            for message_id, raw, stored_created in rows:
                try:
                    message = json.loads(raw)
                except (TypeError, json.JSONDecodeError):
                    continue
                if message.get("role") != "assistant":
                    continue
                tokens = message.get("tokens") or {}
                if not isinstance(tokens, dict) or not tokens:
                    continue
                cache = tokens.get("cache") or {}
                provider = message.get("providerID") or session.get("provider") or "unknown"
                model = message.get("modelID") or session.get("model") or "unknown"
                variant = message.get("variant")
                if variant is None:
                    variant = session.get("variant")
                key = (str(provider), str(model), str(variant) if variant is not None else None)
                agg = aggregates.setdefault(key, {
                    "provider": key[0], "model": key[1], "variant": key[2],
                    "message_count": 0, "tokens_input": 0,
                    "tokens_output": 0, "tokens_reasoning": 0,
                    "tokens_cache_read": 0, "tokens_cache_write": 0,
                    "cost": 0.0, "first_message_id": message_id,
                    "first_message_at": None, "last_message_at": None,
                })
                created = (message.get("time") or {}).get("created") or stored_created
                agg["message_count"] += 1
                agg["tokens_input"] += max(int(tokens.get("input", 0) or 0), 0)
                agg["tokens_output"] += max(int(tokens.get("output", 0) or 0), 0)
                agg["tokens_reasoning"] += max(int(tokens.get("reasoning", 0) or 0), 0)
                agg["tokens_cache_read"] += max(int(cache.get("read", 0) or 0), 0)
                agg["tokens_cache_write"] += max(int(cache.get("write", 0) or 0), 0)
                agg["cost"] += float(message.get("cost", 0) or 0)
                if created:
                    stamp = datetime.fromtimestamp(created / 1000, tz=timezone.utc).isoformat()
                    if agg["first_message_at"] is None:
                        agg["first_message_at"] = stamp
                    agg["last_message_at"] = stamp
            if aggregates:
                by_session[sid] = sorted(
                    aggregates.values(),
                    key=lambda item: (item["first_message_at"] or "", item["provider"],
                                      item["model"], item["variant"] or ""),
                )
                for item in by_session[sid]:
                    item["cost"] = round(item["cost"], 12)
    finally:
        db.close()
    return by_session


def extract_opencode(args, workspace: str) -> dict:
    """Best-effort metadata from the opencode harness (opencode.db). Anything
    that cannot be extracted is emitted as a question for the user."""
    questions: list[dict] = []
    idle_gap = getattr(args, "idle_gap_seconds", 600)
    harness = {
        "harness": "opencode",
        "version": None,
        "config_dir": args.opencode_config_dir,
        "config_entries": None,
        "model_provider": None,
    }
    cfg = Path(args.opencode_config_dir)
    if cfg.is_dir():
        harness["config_entries"] = sorted(p.name for p in cfg.iterdir())

    sessions: list[dict] = []
    try:
        db = sqlite3.connect(f"file:{args.opencode_db}?mode=ro", uri=True)
        try:
            rows = db.execute(
                "SELECT id, parent_id, directory, title, agent, model, "
                "tokens_input, tokens_output, tokens_reasoning, "
                "tokens_cache_read, tokens_cache_write, cost, "
                "time_created, time_updated, version FROM session "
                "WHERE directory = ? OR directory LIKE ? "
                "OR directory = '/workspace' OR directory LIKE '/workspace/%'",
                (workspace, workspace + "/%"),
            ).fetchall()
        finally:
            db.close()
    except sqlite3.Error as exc:
        questions.append({
            "id": "opencode_db",
            "question": "Where is the opencode session database/log for this run?",
            "reason": f"opencode session database is unavailable: {exc}",
            "suggested_source": "workspace .sessions OpenCode DB or an operator-approved "
                                "migration into that bundle",
            "answer": None,
        })
        rows = []

    versions = sorted({str(r[14]) for r in rows if r[14]})
    harness["version"] = ", ".join(versions) if versions else None

    for r in rows:
        m = json.loads(r[5]) if r[5] else {}
        started = (datetime.fromtimestamp(r[12] / 1000, tz=timezone.utc).isoformat()
                   if r[12] else None)
        ended = (datetime.fromtimestamp(r[13] / 1000, tz=timezone.utc).isoformat()
                 if r[13] else None)
        sessions.append({
            "session_id": r[0],
            "parent_id": r[1],
            "directory": r[2],
            "title": r[3],
            "agent": r[4],
            "model": m.get("id") or m.get("modelID"),
            "provider": m.get("providerID"),
            "variant": m.get("variant"),
            "tokens_input": r[6] or 0,
            "tokens_output": r[7] or 0,
            "tokens_reasoning": r[8] or 0,
            "tokens_cache_read": r[9] or 0,
            "tokens_cache_write": r[10] or 0,
            "cost": r[11] or 0.0,
            "time_created": r[12],
            "time_updated": r[13],
            "started_at": started,
            "ended_at": ended,
        })
    sessions = cd.select_opencode_session_trees(
        sessions, cd.parse_roots(getattr(args, "roots", None)))
    message_usage = opencode_message_usage(args.opencode_db, sessions)
    for session in sessions:
        usage = message_usage.get(session["session_id"], [])
        session["usage_by_model"] = usage
        if usage:
            entry = usage[0]
            session["entry_provider"] = entry["provider"]
            session["entry_model"] = entry["model"]
            session["entry_variant"] = entry["variant"]
        else:
            session["entry_provider"] = session["provider"]
            session["entry_model"] = session["model"]
            session["entry_variant"] = session["variant"]
    # per-session activity time from message history (created/completed),
    # excluding idle gaps > threshold (interrupted by user or API).
    activity = opencode_activity_times(args.opencode_db,
                                       [s["session_id"] for s in sessions],
                                       idle_gap)
    for s in sessions:
        s["activity"] = activity.get(s["session_id"], {
            "activity_time_seconds": 0, "wall_time_seconds": 0,
            "idle_time_seconds": 0, "idle_gaps": 0, "events": 0})
    total_activity = sum((a["activity_time_seconds"] for a in activity.values()), 0.0)
    total_idle = sum((a["idle_time_seconds"] for a in activity.values()), 0.0)
    if not sessions:
        questions.append({
            "id": "opencode_sessions",
            "question": "Which opencode sessions belong to this contestant run?",
            "reason": "no opencode sessions found for the workspace in the local database",
            "suggested_source": "opencode session list / opencode export <sessionID>",
            "answer": None,
        })

    roots = [s for s in sessions if not s["parent_id"]]
    children = [s for s in sessions if s["parent_id"]]

    catalog = load_catalog(args.ocx_catalog)
    models: dict[str, dict] = {}
    for s in sessions:
        units = s.get("usage_by_model") or [{
            "model": s["model"], "variant": s["variant"], "provider": s["provider"],
            "tokens_input": s["tokens_input"], "tokens_output": s["tokens_output"],
            "tokens_reasoning": s["tokens_reasoning"],
            "tokens_cache_read": s["tokens_cache_read"],
            "tokens_cache_write": s["tokens_cache_write"], "cost": s["cost"],
        }]
        for unit in units:
            key = (f"{unit['model']}@{unit['variant']}" if unit.get("variant")
                   else (unit.get("model") or "unknown"))
            agg = models.setdefault(key, {
                "model": unit.get("model"), "variant": unit.get("variant"),
                "provider": unit.get("provider"),
                "catalog": {"display_name": None, "context_window": None},
                "reasoning_efforts_seen": ([unit["variant"]]
                                            if unit.get("variant") else []),
                "max_context_used": 0, "threads": 0,
                "tokens_input": 0, "tokens_output": 0, "tokens_reasoning": 0,
                "tokens_cache_read": 0, "tokens_cache_write": 0,
                "cost": 0.0,
            })
            agg["threads"] += 1
            agg["tokens_input"] += unit.get("tokens_input", 0)
            agg["tokens_output"] += unit.get("tokens_output", 0)
            agg["tokens_reasoning"] += unit.get("tokens_reasoning", 0)
            agg["tokens_cache_read"] += unit.get("tokens_cache_read", 0)
            agg["tokens_cache_write"] += unit.get("tokens_cache_write", 0)
            agg["cost"] += unit.get("cost", 0)
        # OpenCode stores cumulative per-session usage, not a per-request
        # context high-water mark. Preserve the historical raw-input proxy;
        # adding cumulative cache reads here would misreport context as tens
        # or hundreds of millions of tokens.
            agg["max_context_used"] = max(agg["max_context_used"],
                                           unit.get("tokens_input", 0))
            if (unit.get("variant")
                    and unit["variant"] not in agg["reasoning_efforts_seen"]):
                agg["reasoning_efforts_seen"].append(unit["variant"])
            if not agg["catalog"]["context_window"]:
                match = next(
                    (m for slug, m in catalog.items()
                     if slug.rsplit("/", 1)[-1]
                     == (unit.get("model") or "").lower()),
                    None,
                )
                if match:
                    agg["catalog"] = {
                        "display_name": match.get("display_name"),
                        "context_window": match.get("context_window"),
                    }
    for key, agg in models.items():
        if not agg["catalog"]["context_window"]:
            questions.append({
                "id": f"context_window_{key.replace('/', '_')}",
                "question": f"What is the context window (max tokens) of model "
                            f"`{agg['model']}`?",
                "reason": "model is not listed in the local model catalog",
                "suggested_source": "provider docs or model card",
                "answer": None,
            })

    subagents = []
    for s in children:
        m = re.search(r"@(\w+)", s["title"] or "")
        typ = m.group(1) if m else (s["agent"] or None)
        subagents.append({
            "session_id": s["session_id"],
            "parent_session_id": s["parent_id"],
            "title": s["title"],
            "type": typ,
            "model": s["model"],
            "variant": s["variant"],
            "tokens_used": (s["tokens_input"] + s["tokens_cache_read"]
                            + s["tokens_cache_write"] + s["tokens_output"]
                            + s["tokens_reasoning"]),
            "cost": s["cost"],
        })

    prompts = {"by_root_session": {}}
    for s in roots[:3]:
        user_msgs = []
        try:
            db = sqlite3.connect(f"file:{args.opencode_db}?mode=ro", uri=True)
            try:
                msgs = db.execute(
                    "SELECT id, data FROM message WHERE session_id=? "
                    "ORDER BY time_created", (s["session_id"],)
                ).fetchall()
                for mid, data in msgs:
                    try:
                        d = json.loads(data)
                    except json.JSONDecodeError:
                        continue
                    if d.get("role") != "user":
                        continue
                    parts = db.execute(
                        "SELECT data FROM part WHERE message_id=?", (mid,)
                    ).fetchall()
                    text = ""
                    for (pdata,) in parts:
                        try:
                            p = json.loads(pdata)
                        except json.JSONDecodeError:
                            continue
                        if p.get("type") == "text":
                            text += p.get("text", "")
                    if not text.strip():
                        continue
                    created = (d.get("time") or {}).get("created")
                    ts = (datetime.fromtimestamp(created / 1000, tz=timezone.utc).isoformat()
                          if created else None)
                    user_msgs.append({"timestamp": ts, "text": text.strip()[:2000]})
                    if len(user_msgs) >= 11:
                        break
            finally:
                db.close()
        except sqlite3.Error:
            pass
        prompts["by_root_session"][s["session_id"]] = {
            "initial_user_prompt": user_msgs[0] if user_msgs else None,
            "resume_prompts": user_msgs[1:],
        }
        if not user_msgs:
            questions.append({
                "id": f"prompts_{s['session_id']}",
                "question": f"What was the initial prompt (and any resume prompts) "
                            f"for opencode session `{s['session_id']}`?",
                "reason": "message content could not be read from the opencode database",
                "suggested_source": "opencode export <sessionID> --sanitize",
                "answer": None,
            })

    context = {
        "by_model": {
            key: {"context_window": agg["catalog"]["context_window"],
                  "max_context_used": agg["max_context_used"]}
            for key, agg in models.items()
        },
        "by_session": {
            s["session_id"]: {"model": s["model"], "variant": s["variant"],
                              "max_input_tokens": s["tokens_input"]}
            for s in sessions
        },
        "notes": [],
    }
    if not workspace_state(Path(workspace))["agents_md"]["exists"]:
        questions.append({
            "id": "agents_md",
            "question": "Which AGENTS.md was in effect for this run? The "
                        "workspace has no AGENTS.md file.",
            "reason": "AGENTS.md contents could not be recorded from the workspace",
            "suggested_source": "the branch's AGENTS.md or the session's world_state",
            "answer": None,
        })
    return {
        "harness": harness,
        "models": models,
        "context": context,
        "subagents": subagents,
        "opencodex": None,
        "opencode": {
            "sessions": sessions,
            "root_session_count": len(roots),
            "subagent_session_count": len(children),
            "activity_time_seconds": round(total_activity, 1),
            "idle_time_seconds": round(total_idle, 1),
            "idle_gap_threshold_seconds": idle_gap,
        },
        "prompts": prompts,
        "workspace": workspace_state(Path(workspace)),
        "session_window": {
            "started_at": min((s["started_at"] for s in sessions if s["started_at"]),
                              default=None),
            "ended_at": max((s["ended_at"] for s in sessions if s["ended_at"]),
                            default=None),
            "activity_time_seconds": round(total_activity, 1),
            "idle_time_seconds": round(total_idle, 1),
            "idle_gap_threshold_seconds": idle_gap,
        },
        "questions": questions,
        "user_answers": {},
        "status": "needs_user_input" if questions else "complete",
        "provenance": {
            "opencode_db": args.opencode_db,
            "opencode_config_dir": args.opencode_config_dir,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "num_sessions": len(sessions),
            "selected_roots": cd.parse_roots(getattr(args, "roots", None)),
            "host_workspace": workspace,
            "recorded_directories": sorted({
                s.get("directory") for s in sessions if s.get("directory")}),
        },
    }


def finalize(metadata: dict, questions: list[dict], answers_path: str | None) -> dict:
    """Record user-provided answers for unextractable metadata. The evaluation
    agent is expected to query the user for anything in `questions` and supply
    the answers via --answers."""
    metadata["questions"] = questions
    metadata["user_answers"] = {}
    if answers_path and Path(answers_path).exists():
        try:
            answers = json.loads(Path(answers_path).read_text())
        except (OSError, json.JSONDecodeError):
            answers = {}
        if isinstance(answers, dict):
            if isinstance(answers.get("user_answers"), dict):
                answers = answers["user_answers"]
            for q in questions:
                if q["id"] in answers:
                    q["answer"] = answers[q["id"]]
            metadata["user_answers"] = answers
    metadata["status"] = (
        "complete" if questions and all(q.get("answer") for q in questions)
        else "needs_user_input" if questions else "complete"
    )
    return metadata


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Extract contestant run metadata")
    ap.add_argument("--workspace", required=True)
    ap.add_argument("--state-db", default=None)
    ap.add_argument("--goals-db", default=None)
    ap.add_argument("--logs-db", default=None)
    ap.add_argument("--sessions-root", default=None)
    ap.add_argument("--history", default=None)
    ap.add_argument("--ocx-config", default=None)
    ap.add_argument("--ocx-catalog", default=None)
    ap.add_argument("--plugins-root", default=None)
    ap.add_argument("--roots", default=None)
    ap.add_argument("--harness", choices=("auto", "codex", "opencode"),
                    default="auto",
                    help="manual harness classification; auto never uses a host store")
    ap.add_argument(
        "--idle-gap-seconds", type=int, default=600,
        help="opencode: gaps between session-history events longer than this "
             "many seconds count as interrupted idle time (default 600)",
    )
    ap.add_argument("--opencode-db", default=None)
    ap.add_argument("--opencode-config-dir", default=None)
    ap.add_argument("--answers", default=None,
                    help="JSON file mapping question ids to user-provided answers "
                         "(evaluation agent asks the user for anything not extractable).")
    eval_root = Path(__file__).resolve().parents[2]
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    workspace = str(Path(args.workspace).resolve())
    paths = cd.local_telemetry_paths(
        workspace, state_db=args.state_db, goals_db=args.goals_db,
        logs_db=args.logs_db, sessions_root=args.sessions_root,
        history=args.history, ocx_config=args.ocx_config,
        ocx_catalog=args.ocx_catalog, plugins_root=args.plugins_root,
        opencode_db=args.opencode_db,
        opencode_config_dir=args.opencode_config_dir)
    for key in ("state_db", "goals_db", "logs_db", "sessions_root", "history",
                "ocx_config", "ocx_catalog", "plugins_root", "opencode_db",
                "opencode_config_dir"):
        setattr(args, key, str(paths[key]))
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / Path(workspace).name / "metadata.json"
    )
    threads = cd.load_threads(args.state_db) if Path(args.state_db).is_file() else {}
    edges = cd.load_spawn_edges(args.state_db) if threads else []
    goals = cd.load_goals(args.goals_db) if Path(args.goals_db).is_file() else {}
    cd.rebase_rollout_paths(threads, args.sessions_root)
    selected = cd.select_threads(threads, workspace)
    roots, all_ids = cd.thread_trees(selected, edges)
    children = {c for _, c in edges}
    requested = cd.parse_roots(args.roots)
    if args.harness == "opencode" or (
            args.harness == "auto" and not all_ids and requested is None
            and not Path(args.state_db).is_file()):
        metadata = extract_opencode(args, workspace)
        metadata = finalize(metadata, metadata.get("questions", []), args.answers)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(metadata, indent=2) + "\n")
        print(f"wrote {out_path}")
        print(f"harness={metadata['harness'].get('harness')} "
              f"version={metadata['harness'].get('version')} "
              f"status={metadata['status']} questions={len(metadata['questions'])}")
        return 0
    if not all_ids and requested is None:
        print("ERROR: no matching project Codex threads; select the harness "
              "manually and pass confirmed Codex --roots when Docker cwd "
              "mapping prevents automatic selection", file=sys.stderr)
        return 2
    if requested is not None:
        missing = [r for r in requested if r not in threads]
        if missing:
            print(f"ERROR: unknown root thread ids: {missing}", file=sys.stderr)
            return 2
        all_ids = set()
        for r in requested:
            all_ids |= cd.tree_of(r, threads, edges)
        roots = requested
    else:
        roots = [r for r in roots if r not in children]

    usage = cd.load_turn_usage(args.logs_db, all_ids) if Path(args.logs_db).is_file() else {}
    catalog = load_catalog(args.ocx_catalog)
    history = load_history(args.history)
    plugins = load_plugins(args.plugins_root)
    questions = []

    # ---- harness ---------------------------------------------------------
    metas = [session_meta(threads[r]["rollout_path"]) for r in roots]
    metas = [m for m in metas if m]
    harness = {
        "harness": "codex",
        "cli_version": metas[0].get("cli_version") if metas else None,
        "originator": metas[0].get("originator") if metas else None,
        "source": metas[0].get("source") if metas else None,
        "thread_source": metas[0].get("thread_source") if metas else None,
        "multi_agent_version": metas[0].get("multi_agent_version") if metas else None,
        "history_mode": metas[0].get("history_mode") if metas else None,
        "memory_mode": metas[0].get("memory_mode") if metas else None,
        "model_provider": metas[0].get("model_provider") if metas else None,
        "codex_runtime_version": None,
        "plugins": plugins,
    }
    rt = Path(args.sessions_root).parent / "codex-runtime.json"
    if rt.exists():
        try:
            harness["codex_runtime_version"] = json.loads(rt.read_text()).get("selectedVersion")
        except json.JSONDecodeError:
            pass

    # ---- threads: models / effort / context / subagents ------------------
    models: dict[str, dict] = {}
    context = {"by_model": {}, "by_thread": {}, "notes": []}
    subagents = []
    threads_out: dict[str, dict] = {}
    for tid in sorted(all_ids):
        t = threads.get(tid)
        if not t:
            continue
        recs = usage.get(tid, [])
        efforts = thread_efforts(t["rollout_path"], recs)
        rollout_facts = cd.rollout_usage_facts(t["rollout_path"])
        entry_settings = cd.rollout_entry_settings(t["rollout_path"])
        max_input = max((r["input_tokens"] for r in recs), default=None)
        mean_input = (sum(r["input_tokens"] for r in recs) / len(recs)) if recs else None
        if max_input is None:
            max_input = rollout_facts["max_prompt_input_tokens"]
            mean_input = rollout_facts["mean_prompt_input_tokens"]
        is_sub = tid in children
        entry = {
            "thread_id": tid,
            "is_subagent": is_sub,
            "model": t["model"],
            "entry_model": entry_settings["model"] or t["model"],
            "model_provider": t["model_provider"],
            "reasoning_effort": efforts or None,
            "entry_reasoning_effort": entry_settings["reasoning_effort"],
            "tokens_used": t["tokens_used"],
            "cwd": t["cwd"],
            "git_branch": t["git_branch"],
            "created_at": t["created_at"],
            "updated_at": t["updated_at"],
            "started_at": None,
            "ended_at": None,
            "context_used_max_input": max_input,
            "context_used_mean_input": round(mean_input, 1) if mean_input is not None else None,
            "model_context_window_recorded": rollout_facts["model_context_window"],
        }
        s, e = cd.session_window(t["rollout_path"])
        entry["started_at"] = (
            s.isoformat() if s
            else datetime.fromtimestamp(t["created_at"], tz=timezone.utc).isoformat()
        )
        entry["ended_at"] = (
            e.isoformat() if e
            else datetime.fromtimestamp(t["updated_at"], tz=timezone.utc).isoformat()
        )
        threads_out[tid] = entry
        if is_sub:
            parent = next((p for p, c in edges if c == tid), None)
            subagents.append({
                "thread_id": tid,
                "parent_thread_id": parent,
                "nickname": t.get("agent_nickname"),
                "agent_path": t.get("agent_path"),
                "type": (t.get("agent_path") or "").rstrip("/").split("/")[-1] or None,
                "model": t["model"],
                "reasoning_effort": efforts or None,
                "tokens_used": t["tokens_used"],
            })
        m = t["model"]
        cat = catalog.get((m or "").lower())
        models.setdefault(m, {
            "catalog": {
                "display_name": cat.get("display_name") if cat else None,
                "context_window": cat.get("context_window") if cat else None,
                "max_context_window": cat.get("max_context_window") if cat else None,
                "supported_reasoning_levels": [
                    x.get("effort") for x in (cat.get("supported_reasoning_levels") or [])
                ] if cat else None,
            },
            "reasoning_efforts_seen": [],
            "max_context_used": 0,
            "threads": 0,
        })
        if models[m]["catalog"]["context_window"] is None and rollout_facts["model_context_window"]:
            models[m]["catalog"]["context_window"] = rollout_facts["model_context_window"]
        for e in efforts or []:
            if e not in models[m]["reasoning_efforts_seen"]:
                models[m]["reasoning_efforts_seen"].append(e)
        if max_input is not None:
            models[m]["max_context_used"] = max(models[m]["max_context_used"], max_input)
        models[m]["threads"] += 1
        context["by_thread"][tid] = {
            "model": m, "max_input_tokens": max_input,
            "mean_input_tokens": round(mean_input, 1) if mean_input is not None else None,
            "model_context_window_recorded": rollout_facts["model_context_window"],
        }
    for m, info in models.items():
        context["by_model"][m] = {
            "context_window": info["catalog"]["context_window"],
            "max_context_used": info["max_context_used"],
        }
        if not info["max_context_used"]:
            context["notes"].append(
                f"no per-turn usage logs for {m}; observed context window unknown"
            )

    # ---- opencodex router (non-vanilla model set only) -------------------
    opencodex = None
    non_vanilla = sorted({m for m in models if (m or "").lower() not in VANILLA_MODELS})
    if non_vanilla:
        opencodex = {
            "trigger": "non_vanilla_models",
            "non_vanilla_models": non_vanilla,
            "opencodex_version": None,
            "opencodex_submodule_pin": None,
            "config_facts": {},
            "codex_proxy_fallback_config": None,
        }
        sub = Path(__file__).resolve().parents[2] / "opencodex"
        opencodex["opencodex_submodule_pin"] = _run(
            ["git", "-C", str(sub), "describe", "--tags"]) if sub.exists() else None
        cfg = Path(args.ocx_config)
        version_file = cfg.parent / "version.json"
        if version_file.is_file():
            try:
                version_doc = json.loads(version_file.read_text())
                opencodex["opencodex_version"] = (
                    version_doc.get("version") or version_doc.get("selectedVersion"))
            except (OSError, json.JSONDecodeError):
                pass
        if cfg.exists():
            try:
                c = json.loads(cfg.read_text())
            except json.JSONDecodeError:
                c = {}
            prov = c.get("providers") or {}
            opencodex["config_facts"] = {
                "default_provider": c.get("defaultProvider"),
                "providers": sorted(prov.keys()),
                "multi_agent_mode": c.get("multiAgentMode"),
                "subagent_models": c.get("subagentModels"),
                "disabled_models": c.get("disabledModels"),
                "context_cap_value": c.get("contextCapValue"),
                "provider_effort_maps": {
                    name: (p.get("modelReasoningEffortMap")
                           or p.get("modelReasoningEfforts")
                           or p.get("modelDefaultReasoningEfforts"))
                    for name, p in prov.items()
                    if (p.get("modelReasoningEffortMap")
                        or p.get("modelReasoningEfforts")
                        or p.get("modelDefaultReasoningEfforts"))
                },
            }
        fallback = Path(args.sessions_root).parent / "opencodex.config.toml"
        if fallback.exists():
            opencodex["codex_proxy_fallback_config"] = str(fallback)

    # ---- questions: anything the evaluator could not extract -------------
    if not workspace_state(Path(workspace))["agents_md"]["exists"]:
        questions.append({
            "id": "agents_md",
            "question": "Which AGENTS.md was in effect for this run? The "
                        "workspace has no AGENTS.md file.",
            "reason": "AGENTS.md contents could not be recorded from the workspace",
            "suggested_source": "the branch's AGENTS.md or the session's world_state",
            "answer": None,
        })
    for m, info in models.items():
        if not info["catalog"].get("context_window"):
            questions.append({
                "id": f"context_window_{m.replace('/', '_')}",
                "question": f"What is the context window (max tokens) of model `{m}`?",
                "reason": "model is not listed in the model catalog",
                "suggested_source": "provider docs or model card",
                "answer": None,
            })
        if (m or "").lower() in VANILLA_MODELS:
            continue
        if not info["reasoning_efforts_seen"]:
            questions.append({
                "id": f"effort_{m.replace('/', '_')}",
                "question": f"Which reasoning effort/mode was used for model `{m}`?",
                "reason": "codex does not record reasoning effort for router-managed "
                          "(non-vanilla) models",
                "suggested_source": "bundled .sessions OpenCodex config effort map "
                                    "or persisted rollout settings",
                "answer": None,
            })

    # ---- prompts ---------------------------------------------------------
    prompts = {"by_root_thread": {}}
    for rid in roots:
        t = threads.get(rid)
        if not t:
            continue
        h = history.get(rid, [])
        goal = goals.get(rid)
        if h:
            initial = h[0]
            resumes = h[1:]
            source = "history"
        else:
            fp = fallback_prompts(t["rollout_path"])
            initial = {"timestamp": fp[0]["timestamp"], "text": fp[0]["text"]} if fp else None
            resumes = fp[1:] if fp else []
            source = "session_rollout"
        prompts["by_root_thread"][rid] = {
            "goal_objective": goal.get("objective") if goal else None,
            "goal_status": goal.get("status") if goal else None,
            "initial_user_prompt": initial,
            "resume_prompts": resumes,
            "source": source,
        }

    metadata = {
        "harness": harness,
        "models": models,
        "context": context,
        "threads": threads_out,
        "subagents": subagents,
        "opencodex": opencodex,
        "prompts": prompts,
        "workspace": workspace_state(Path(workspace)),
        "session_window": {
            "started_at": min((x["started_at"] for x in threads_out.values()),
                              default=None),
            "ended_at": max((x["ended_at"] for x in threads_out.values()),
                            default=None),
        },
        "provenance": {
            "state_db": args.state_db,
            "goals_db": args.goals_db,
            "logs_db": args.logs_db,
            "sessions_root": args.sessions_root,
            "history": args.history,
            "ocx_config": args.ocx_config,
            "ocx_catalog": args.ocx_catalog,
            "plugins_root": args.plugins_root,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "num_threads": len(all_ids),
        },
    }
    metadata = finalize(metadata, questions, args.answers)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"wrote {out_path}")
    print(f"harness={harness['harness']} cli={harness['cli_version']} "
          f"threads={len(all_ids)} subagents={len(subagents)} "
          f"non_vanilla={non_vanilla or 'no'} status={metadata['status']} "
          f"questions={len(metadata['questions'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Session discovery and bucketed session analysis for a contestant run.

Two classes of sessions are discovered:

1. **System-level**: codex sessions in `~/.codex` (state_5.sqlite + rollouts)
   and opencode sessions in `~/.local/share/opencode/opencode.db`, filtered to
   the contestant workspace.
2. **Project-isolated**: a copy bundled inside the workspace, conventionally
   under `<workspace>/.sessions/codex/` (state_5.sqlite + sessions/) and
   `<workspace>/.sessions/opencode-data/` (opencode.db). The workspace setup
   script creates `.sessions/` for exactly this purpose.

The script discovers and analyzes; the evaluation agent classifies/selects
(roots, source) when the discovery is ambiguous, answering via
`--session-answers <json>`.

Analysis produces **30-minute buckets** over the merged timeline of the main
thread/session and all subagents: token usage (incl. cache-hit history),
shell-call categories, tool-call statistics. Whole-length statistics are kept
too. Idle periods (no events in ANY thread/session) are excluded; gaps that
look like permission-blocked waits are reported as candidates with an honest
limitation note.

Usage:
  python3 evaluation/tools/extract_sessions.py --workspace <contestant-workspace>
    [--out PATH] [--state-db PATH] [--sessions-root PATH] [--opencode-db PATH]
    [--session-source system|project|all] [--session-answers FILE]
    [--bucket-seconds 1800] [--idle-gap-seconds 600] [--roots IDS]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
from collections import Counter
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cfdeval import codex_data as cd


DEFAULT_BUCKET_SECONDS = 1800

TOOL_CATEGORIES: dict[str, set[str]] = {
    "shell": {"exec_command", "exec", "write_stdin", "bash", "zsh",
              "powershell", "cmd"},
    "editing": {"apply_patch", "edit", "write", "create_file", "edit_file",
                "str_replace_editor", "multi_tool_use.parallel"},
    "files": {"read", "grep", "rg", "glob", "ls", "find", "view_image",
              "list_mcp_resources", "list_mcp_resource_templates",
              "read_mcp_resource", "cat", "head", "tail", "file"},
    "collaboration": {"spawn_agent", "followup_task", "send_message",
                      "wait_agent", "wait", "interrupt_agent", "list_agents"},
    "web": {"curl", "wget", "fetch", "web_search", "web_fetch", "browser"},
    "package": {"npm", "pip", "pip3", "uv", "cargo", "apt", "apt-get", "docker"},
    "mcp": {"tool_search"},
    "skill": {"skill", "skills"},
    "interaction": {"ask", "ask_user", "request_user_input"},
    "planning": {"todo", "update_plan", "create_goal", "update_goal", "get_goal"},
}


def tool_category(name: str) -> str:
    for cat, names in TOOL_CATEGORIES.items():
        if name in names:
            return cat
    if name.startswith("mcp__"):
        return "mcp"
    return "other"


def _iso(ts_ms: int | None) -> str | None:
    if not ts_ms:
        return None
    return datetime.fromtimestamp(ts_ms / 1000, tz=timezone.utc).isoformat()


def parse_ts(text: str | None):
    return cd.parse_iso(text)


# --------------------------------------------------------------------------
# Codex analysis
# --------------------------------------------------------------------------

class CodexThreadEvents:
    """Aggregated per-thread event stream from a rollout file."""

    def __init__(self, thread_id: str, rollout_path: str | None):
        self.thread_id = thread_id
        self.rollout_path = rollout_path
        self._prev_tot: dict = {}
        self.cumulative_final: int | None = None   # final total_token_usage (thread total)
        self.cumulative_usage: dict[str, int] = {
            "input_tokens": 0, "cached_input_tokens": 0,
            "cache_write_input_tokens": 0, "output_tokens": 0,
            "reasoning_output_tokens": 0, "total_tokens": 0,
        }
        self.model_context_window: int | None = None
        self.events: list[tuple[datetime, str, dict]] = []  # (ts, kind, info)
        self.token_events: list[tuple[datetime, dict]] = []
        self.tool_events: list[tuple[datetime, str]] = []
        self.turn_events: list[tuple[datetime, str]] = []
        self.user_messages: list[tuple[datetime, str]] = []
        self.settings: list[tuple[datetime, dict]] = []
        self.first_ts: datetime | None = None
        self.last_ts: datetime | None = None
        self.sha256: str | None = None
        self.bytes = 0

    def add(self, ts: datetime, kind: str, info: dict) -> None:
        self.events.append((ts, kind, info))
        if self.first_ts is None or ts < self.first_ts:
            self.first_ts = ts
        if self.last_ts is None or ts > self.last_ts:
            self.last_ts = ts

    def load(self) -> None:
        p = Path(self.rollout_path) if self.rollout_path else None
        if not p or not p.exists():
            return
        self.bytes = p.stat().st_size
        self.sha256 = hashlib.sha256(p.read_bytes()).hexdigest() if self.bytes < 64 * 1024 * 1024 else None
        for rec in cd.iter_session_records(str(p)):
            ts = parse_ts(rec.get("timestamp"))
            if ts is None:
                continue
            rtype = rec.get("type")
            payload = rec.get("payload") or {}
            if rtype == "event_msg":
                etype = payload.get("type")
                if etype == "token_count":
                    # Legacy rollouts may emit a rate-limit-only token_count
                    # event with an explicit JSON null for `info`.
                    info = payload.get("info") or {}
                    tot = info.get("total_token_usage") or {}
                    if tot:
                        # `total_token_usage` is the thread's cumulative usage
                        # counter (== threads.tokens_used at session end);
                        # `last_token_usage` is the usage of the most recent
                        # submission. Per-event usage = delta between
                        # consecutive total_token_usage readings: this is
                        # identical to last_token_usage for real submissions,
                        # but naturally drops duplicate ticks (streaming /
                        # periodic re-reports) and survives counter resets.
                        fields = ("input_tokens", "cached_input_tokens",
                                  "cache_write_input_tokens", "output_tokens",
                                  "reasoning_output_tokens", "total_tokens")
                        cur = {k: max(tot.get(k, 0), 0) for k in fields}
                        if cur["total_tokens"] < self._prev_tot.get("total_tokens", 0):
                            # counter reset (compaction): count the new epoch
                            # once from its own base.
                            delta = cur
                        else:
                            delta = {k: cur[k] - self._prev_tot.get(k, 0)
                                     for k in fields}
                        if delta["total_tokens"] > 0:
                            self.token_events.append((ts, delta))
                            for key in fields:
                                self.cumulative_usage[key] += delta[key]
                        self._prev_tot = cur
                        self.cumulative_final = cur["total_tokens"]
                        context_window = info.get("model_context_window")
                        if isinstance(context_window, int) and context_window > 0:
                            self.model_context_window = context_window
                    self.add(ts, "token_count", {"info": payload.get("info", {})})
                elif etype in ("task_started", "task_complete", "turn_aborted",
                               "context_compacted", "sub_agent_activity",
                               "agent_message", "thread_goal_updated"):
                    self.turn_events.append((ts, etype))
                    self.add(ts, etype, {})
                elif etype == "user_message":
                    self.user_messages.append((ts, payload.get("message", "")[:200]))
                    self.add(ts, "user_message", {})
                elif etype == "thread_settings_applied":
                    st = payload.get("thread_settings") or {}
                    self.settings.append((ts, {
                        "approval_policy": st.get("approval_policy"),
                        "approvals_reviewer": st.get("approvals_reviewer"),
                        "model": st.get("model"),
                    }))
                    self.add(ts, "settings", {})
            elif rtype == "turn_context":
                self.settings.append((ts, {
                    "approval_policy": payload.get("approval_policy"),
                    "approvals_reviewer": payload.get("approvals_reviewer"),
                    "model": payload.get("model"),
                }))
                self.add(ts, "turn_context", {})
            elif rtype == "response_item":
                ptype = payload.get("type")
                if ptype in ("function_call", "custom_tool_call"):
                    name = payload.get("name") or "unknown"
                    self.tool_events.append((ts, name))
                    self.add(ts, "tool_call", {"name": name})
                elif ptype == "message":
                    self.add(ts, "message", {"role": payload.get("role")})
                else:
                    self.add(ts, "response_item", {})
            else:
                self.add(ts, rtype or "record", {})


def analyze_codex(workspace: str, state_db: str, sessions_root: str,
                  roots: list[str] | None) -> dict:
    """Discover codex threads for the workspace and return per-thread event
    streams plus root trees. Returns (doc_section, thread_events, children)."""
    threads = cd.load_threads(state_db)
    edges = cd.load_spawn_edges(state_db)
    selected = cd.select_threads(threads, workspace)
    root_ids, all_ids = cd.thread_trees(selected, edges)
    children = {c for _, c in edges}
    if roots:
        root_ids = [r for r in roots if r in threads]
        all_ids = set()
        for r in root_ids:
            all_ids |= cd.tree_of(r, threads, edges)

    streams: dict[str, CodexThreadEvents] = {}
    for tid in sorted(all_ids):
        t = threads.get(tid)
        if not t:
            continue
        s = CodexThreadEvents(tid, t.get("rollout_path"))
        s.load()
        streams[tid] = s

    missing_rollouts = sorted(
        tid for tid in all_ids
        if threads.get(tid) and not threads[tid].get("rollout_path")
    )
    roots_out = []
    for rid in root_ids:
        tree = cd.tree_of(rid, threads, edges)
        members = [streams[t] for t in tree if t in streams]
        starts = [m.first_ts for m in members if m.first_ts]
        ends = [m.last_ts for m in members if m.last_ts]
        roots_out.append({
            "thread_id": rid,
            "model": threads.get(rid, {}).get("model"),
            "thread_count": len(tree),
            "started_at": min(starts).isoformat() if starts else None,
            "ended_at": max(ends).isoformat() if ends else None,
            "rollout_bytes": sum(m.bytes for m in members),
        })

    all_events = []
    for tid, s in streams.items():
        for ts, kind, info in s.events:
            all_events.append((ts, "codex", tid, kind, info))
    all_events.sort(key=lambda x: x[0])

    doc = {
        "present": bool(streams),
        "source_db": state_db,
        "sessions_root": sessions_root,
        "thread_count": len(streams),
        "subagent_count": sum(1 for t in streams if t in children),
        "root_count": len(root_ids),
        "roots": roots_out,
        "rollout_bytes_total": sum(s.bytes for s in streams.values()),
        "missing_rollouts": missing_rollouts,
    }
    return doc, streams, all_events, children


# --------------------------------------------------------------------------
# opencode analysis
# --------------------------------------------------------------------------

def opencode_sessions(opencode_db: str, workspace: str) -> list[dict]:
    out = []
    try:
        db = sqlite3.connect(f"file:{opencode_db}?mode=ro", uri=True)
    except sqlite3.Error:
        return out
    try:
        rows = db.execute(
            "SELECT id, parent_id, directory, title, agent, model, "
            "tokens_input, tokens_output, tokens_reasoning, tokens_cache_read, "
            "tokens_cache_write, cost, time_created, time_updated, version "
            "FROM session WHERE directory = ? OR directory LIKE ?",
            (workspace, workspace + "/%"),
        ).fetchall()
    except sqlite3.Error:
        rows = []
    finally:
        db.close()
    for r in rows:
        m = json.loads(r[5]) if r[5] else {}
        out.append({
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
            "version": r[14],
        })
    return out


def analyze_opencode(opencode_db: str, workspace: str) -> dict:
    """Return (doc_section, all_events). Events carry per-message token usage
    and per-part tool calls so bucketing covers cache history and tools."""
    sessions = opencode_sessions(opencode_db, workspace)
    by_id = {s["session_id"]: s for s in sessions}
    children = {s["session_id"] for s in sessions if s["parent_id"]}
    all_events: list[tuple[datetime, str, str, str, dict]] = []
    token_events: list[tuple[datetime, dict, str]] = []
    tool_events: list[tuple[datetime, str, str]] = []

    if sessions:
        try:
            db = sqlite3.connect(f"file:{opencode_db}?mode=ro", uri=True)
            try:
                for s in sessions:
                    sid = s["session_id"]
                    msgs = db.execute(
                        "SELECT data FROM message WHERE session_id=? "
                        "ORDER BY time_created", (sid,)).fetchall()
                    for (data,) in msgs:
                        try:
                            d = json.loads(data)
                        except json.JSONDecodeError:
                            continue
                        t = d.get("time") or {}
                        created = t.get("created")
                        completed = t.get("completed")
                        if created:
                            ts = datetime.fromtimestamp(created / 1000, tz=timezone.utc)
                            tok = d.get("tokens") or {}
                            cache = tok.get("cache") or {}
                            input_raw = tok.get("input", 0)
                            cache_read = cache.get("read", 0)
                            usage = {
                                # opencode counts cache reads separately from
                                # input; normalize to codex semantics
                                # (input includes cached):
                                "input": input_raw + cache_read,
                                "cached": cache_read,
                                "non_cached": input_raw,
                                "output": tok.get("output", 0),
                                "reasoning": tok.get("reasoning", 0),
                                "total": tok.get("total", 0),
                            }
                            all_events.append((ts, "opencode", sid,
                                               "message", {
                                                   "role": d.get("role"),
                                                   "finish": d.get("finish"),
                                               }))
                            if any(usage.values()):
                                token_events.append((ts, usage, sid))
                        if completed:
                            ts = datetime.fromtimestamp(completed / 1000, tz=timezone.utc)
                            all_events.append((ts, "opencode", sid, "message_complete", {}))
                    parts = db.execute(
                        "SELECT data, time_created FROM part WHERE session_id=? "
                        "AND data LIKE '%\"type\":\"tool\"%'", (sid,)).fetchall()
                    for pdata, ptc in parts:
                        try:
                            p = json.loads(pdata)
                        except json.JSONDecodeError:
                            continue
                        if p.get("type") != "tool":
                            continue
                        name = p.get("tool") or "unknown"
                        ts = None
                        if ptc:
                            ts = datetime.fromtimestamp(ptc / 1000, tz=timezone.utc)
                        if ts is None:
                            continue
                        tool_events.append((ts, name, sid))
                        all_events.append((ts, "opencode", sid, "tool_call",
                                           {"name": name}))
            finally:
                db.close()
        except sqlite3.Error:
            pass

    doc = {
        "present": bool(sessions),
        "source_db": opencode_db,
        "session_count": len(sessions),
        "root_session_count": len(sessions) - len(children),
        "subagent_session_count": len(children),
        "sessions": [
            {
                "session_id": s["session_id"],
                "parent_id": s["parent_id"],
                "agent": s["agent"],
                "model": s["model"],
                "variant": s["variant"],
                "title": s["title"],
                "tokens_input": s["tokens_input"],
                "tokens_output": s["tokens_output"],
                "tokens_cache_read": s["tokens_cache_read"],
                "started_at": _iso(s["time_created"]),
                "ended_at": _iso(s["time_updated"]),
            }
            for s in sessions
        ],
    }
    return doc, all_events, token_events, tool_events


# --------------------------------------------------------------------------
# Merged timeline, idle gaps, buckets
# --------------------------------------------------------------------------

def merged_timeline(all_events):
    """Sort events; return list sorted by ts."""
    return sorted(all_events, key=lambda x: x[0])


def active_intervals(timeline, idle_gap_seconds: int):
    """Split the sorted merged timeline into active intervals separated by
    gaps longer than idle_gap_seconds (no events in any thread/session)."""
    if not timeline:
        return [], []
    intervals = []
    gaps = []
    seg_start = timeline[0][0]
    prev = timeline[0]
    for ev in timeline[1:]:
        gap = (ev[0] - prev[0]).total_seconds()
        if gap > idle_gap_seconds:
            intervals.append((seg_start, prev[0]))
            gaps.append({
                "start": prev[0].isoformat(),
                "end": ev[0].isoformat(),
                "seconds": round(gap, 1),
                "spanning_entities": sorted({prev[2], ev[2]}),
            })
            seg_start = ev[0]
        prev = ev
    intervals.append((seg_start, prev[0]))
    return intervals, gaps


def permission_wait_candidates(timeline, gaps, idle_gap_seconds: int,
                               streams_settings, tool_events=None) -> dict:
    """Heuristic recognition of permission-blocked idle periods.

    Codex rollouts on this host contain no explicit approval-request events,
    so candidates are gaps whose preceding event is a tool call / turn
    boundary and whose following event is a user message or new turn, in a
    thread whose settings allow approval prompts (approval_policy != never or
    approvals_reviewer == user). This may also catch ordinary user-away time;
    the limitation is reported explicitly.
    """
    candidates = []
    policy_observed = set()
    ask_tools = {"ask", "ask_user", "request_user_input", "permission"}
    ask_events = sorted(
        (ts, eid, name) for ts, name, eid in (tool_events or [])
        if name in ask_tools
    )
    for tid, settings in streams_settings.items():
        for _ts, st in settings:
            if st.get("approval_policy"):
                policy_observed.add(f"{st['approval_policy']}")
            if st.get("approvals_reviewer"):
                policy_observed.add(f"reviewer={st['approvals_reviewer']}")

    idx = 0
    # Map each gap to the events immediately before/after in the merged stream.
    for g in gaps:
        g_start = parse_ts(g["start"])
        g_end = parse_ts(g["end"])
        before = [e for e in timeline if e[0] <= g_start]
        after = [e for e in timeline if e[0] >= g_end]
        if not before or not after:
            continue
        b = before[-1]
        a = after[0]
        bkind = b[3]
        akind = a[3]
        bthread = b[2]
        binfo = b[4]
        # opencode: a message that finished with "ask" (user-question tool)
        # right before the gap is a strong permission-wait signal.
        ask_finish = bool(bkind == "message" and binfo.get("finish") == "ask")
        ask_tool_before = any(
            (g_start - ts).total_seconds() <= 120 and (g_start - ts).total_seconds() >= 0
            for ts, eid, _name in ask_events if eid == bthread
        )
        is_tool_boundary = bkind in ("tool_call", "turn_context", "task_started",
                                     "message", "settings")
        is_resume = akind in ("user_message", "task_started", "turn_context",
                              "message")
        if not (is_tool_boundary and is_resume):
            continue
        if ask_finish or ask_tool_before:
            candidates.append({
                "start": g["start"],
                "end": g["end"],
                "seconds": g["seconds"],
                "thread_id": bthread,
                "preceding_event": bkind,
                "following_event": akind,
                "approval_aware_thread": True,
                "reason": "gap follows an ask/user-question tool or an "
                          "'ask'-finished message; high-confidence "
                          "permission-blocked idle candidate",
            })
            continue
        approval_aware = False
        settings = streams_settings.get(bthread, [])
        for _ts, st in reversed(settings):
            if st.get("approval_policy") in ("on-request", "untrusted",
                                             "require-escalated"):
                approval_aware = True
                break
            if st.get("approvals_reviewer") == "user":
                approval_aware = True
                break
        candidates.append({
            "start": g["start"],
            "end": g["end"],
            "seconds": g["seconds"],
            "thread_id": bthread,
            "preceding_event": bkind,
            "following_event": akind,
            "approval_aware_thread": approval_aware,
            "reason": "idle gap between tool/turn boundary and a user message "
                      "or new turn; may be a permission wait or user away",
        })
        idx += 1

    return {
        "method": "heuristic",
        "explicit_approval_events_found": bool(ask_events),
        "ask_tool_events_found": len(ask_events),
        "candidate_gaps": candidates[:50],
        "candidate_count": len(candidates),
        "policy_observed": sorted(policy_observed),
        "limitations": [
            "codex rollouts contain no explicit approval-request/response "
            "event types (verified by scanning all local sessions); "
            "permission-blocked idle cannot be distinguished from user-away "
            "time with certainty",
            "candidates are gaps > idle threshold between a tool/turn "
            "boundary and a user message / new turn in approval-aware "
            "threads; expect false positives when the user stepped away",
        ],
    }


def build_buckets(window_start: datetime, window_end: datetime,
                  bucket_seconds: int, intervals,
                  token_events, tool_events, turn_events,
                  entity_events) -> list[dict]:
    """Aggregate per-bucket statistics. Token/tool/turn events are assigned by
    timestamp; active_seconds comes from the active intervals (idle excluded)."""
    buckets = []
    if window_start is None:
        return buckets
    total = max(int((window_end - window_start).total_seconds() // bucket_seconds) + 1, 1)
    for i in range(total):
        b_start = window_start + timedelta(seconds=i * bucket_seconds)
        b_end = min(b_start + timedelta(seconds=bucket_seconds), window_end)
        # active seconds = overlap of active intervals with [b_start, b_end]
        active_seconds = 0.0
        for (a0, a1) in intervals:
            lo = max(a0, b_start)
            hi = min(a1, b_end)
            if hi > lo:
                active_seconds += (hi - lo).total_seconds()
        active_seconds = round(min(active_seconds, bucket_seconds), 1)
        idle_seconds = round(max(0.0, (b_end - b_start).total_seconds() - active_seconds), 1)
        tok = {"input": 0, "cached_input": 0, "non_cached_input": 0,
               "output": 0, "reasoning_output": 0, "total": 0}
        for ts, usage, _eid in token_events:
            if b_start <= ts < b_end:
                tok["input"] += usage.get("input", 0)
                tok["cached_input"] += usage.get("cached", 0)
                tok["non_cached_input"] += usage.get("non_cached", 0)
                tok["output"] += usage.get("output", 0)
                tok["reasoning_output"] += usage.get("reasoning", 0)
                tok["total"] += usage.get("total", 0)
        tools = Counter()
        tool_categories = Counter()
        for ts, name, _eid in tool_events:
            if b_start <= ts < b_end:
                tools[name] += 1
                tool_categories[tool_category(name)] += 1
        turns = Counter()
        for ts, kind in turn_events:
            if b_start <= ts < b_end:
                turns[kind] += 1
        active_entities = {
            eid for ts, eid in entity_events if b_start <= ts < b_end
        }
        buckets.append({
            "bucket_index": i,
            "start": b_start.isoformat(),
            "end": b_end.isoformat(),
            "active_seconds": active_seconds,
            "idle_seconds": idle_seconds,
            "active": active_seconds > 0,
            "tokens": tok,
            "cache": {
                "cached_tokens": tok["cached_input"],
                "input_tokens": tok["input"],
                "hit_ratio": round(tok["cached_input"] / tok["input"], 4)
                if tok["input"] else None,
            },
            "tools": {
                "total": sum(tools.values()),
                "by_tool": dict(sorted(tools.items(), key=lambda kv: -kv[1])),
                "by_category": dict(sorted(
                    tool_categories.items(),
                    key=lambda kv: -kv[1])),
            },
            "turns": dict(turns),
            "active_entities": sorted(active_entities),
            "active_entity_count": len(active_entities),
        })
    return buckets


def whole_stats(buckets, token_events, tool_events, turn_events,
                intervals, per_entity, run_total_codex=None,
                run_total_opencode=None, accounting_notes=None) -> dict:
    tok = {"input": 0, "cached_input": 0, "non_cached_input": 0,
           "output": 0, "reasoning_output": 0, "total": 0}
    for _ts, usage, _eid in token_events:
        tok["input"] += usage.get("input", 0)
        tok["cached_input"] += usage.get("cached", 0)
        tok["non_cached_input"] += usage.get("non_cached", 0)
        tok["output"] += usage.get("output", 0)
        tok["reasoning_output"] += usage.get("reasoning", 0)
        tok["total"] += usage.get("total", 0)
    tools = Counter()
    tool_categories = Counter()
    for _ts, name, _eid in tool_events:
        tools[name] += 1
        tool_categories[tool_category(name)] += 1
    turns = Counter()
    for ts, kind in turn_events:
        turns[kind] += 1
    active_seconds = sum((a1 - a0).total_seconds() for a0, a1 in intervals)
    return {
        "tokens": {
            **tok,
            "total_from_root_trees": run_total_codex,
            "total_from_all_sessions": run_total_opencode,
        },
        "cache": {
            "cached_tokens": tok["cached_input"],
            "input_tokens": tok["input"],
            "hit_ratio": round(tok["cached_input"] / tok["input"], 4)
            if tok["input"] else None,
        },
        "tools": {
            "total": sum(tools.values()),
            "by_tool": dict(sorted(tools.items(), key=lambda kv: -kv[1])),
            "by_category": dict(sorted(
                tool_categories.items(),
                key=lambda kv: -kv[1])),
        },
        "turns": dict(turns),
        "activity": {
            "active_seconds": round(active_seconds, 1),
            "wall_seconds": round((intervals[-1][1] - intervals[0][0]).total_seconds(), 1)
            if intervals else 0.0,
            "idle_seconds": round(
                (intervals[-1][1] - intervals[0][0]).total_seconds() - active_seconds, 1)
            if intervals else 0.0,
        },
        "per_entity": per_entity,
        "accounting_notes": accounting_notes or [],
    }


def _norm_codex_usage(u: dict) -> dict:
    """Codex `last_token_usage` delta → normalized usage dict.

    Codex semantics: input_tokens INCLUDES cached_input_tokens. Clamp cached
    to input and recompute non-cached per event so cumulative-reset artifacts
    (negative or oversized deltas) cannot distort bucket sums.
    """
    input_t = max(u.get("input_tokens", 0), 0)
    cached_t = max(u.get("cached_input_tokens", 0), 0)
    cached_t = min(cached_t, input_t)
    return {
        "input": input_t,
        "cached": cached_t,
        "non_cached": input_t - cached_t,
        "output": u.get("output_tokens", 0),
        "reasoning": u.get("reasoning_output_tokens", 0),
        "total": u.get("total_tokens", 0),
    }


def analyze(workspace: str, state_db: str, sessions_root: str,
            opencode_db: str, bucket_seconds: int, idle_gap_seconds: int,
            session_source: str, roots: list[str] | None,
            project_codex_root: Path | None,
            project_opencode_db: Path | None) -> dict:
    """Main analysis entry: discover sources, merge timelines, bucket."""
    codex_docs = []
    oc_docs = []
    all_events: list = []
    token_events: list = []
    tool_events: list = []
    turn_events: list = []
    entity_events: list = []
    streams_settings: dict[str, list] = {}
    per_entity: dict[str, dict] = {}
    codex_children: set[str] = set()
    streams: dict[str, CodexThreadEvents] = {}
    codex_run_total = 0

    use_system_codex = session_source in ("system", "all")
    use_project_codex = session_source in ("project", "all")
    use_system_oc = session_source in ("system", "all")
    use_project_oc = session_source in ("project", "all")

    if use_system_codex:
        doc, streams, events, children = analyze_codex(
            workspace, state_db, sessions_root, roots)
        if doc["present"]:
            doc["source"] = "system"
            codex_docs.append(doc)
            codex_children |= children
            for tid, s in streams.items():
                codex_run_total += s.cumulative_final or 0
            all_events.extend(events)
            for tid, s in streams.items():
                token_events.extend(
                    (ts, _norm_codex_usage(u), tid) for ts, u in s.token_events)
                tool_events.extend((ts, name, tid) for ts, name in s.tool_events)
                turn_events.extend((ts, kind) for ts, kind in s.turn_events)
                entity_events.extend((ts, tid) for ts, _k, _i in s.events)
                streams_settings[tid] = s.settings
                per_entity[tid] = {
                    "kind": "codex_thread",
                    "is_subagent": tid in children,
                    "events": len(s.events),
                    "tokens_total_subtree": s.cumulative_final,
                    "tools": dict(Counter(n for _ts, n in s.tool_events)),
                    "first_event": s.first_ts.isoformat() if s.first_ts else None,
                    "last_event": s.last_ts.isoformat() if s.last_ts else None,
                    "rollout_bytes": s.bytes,
                }
    if use_project_codex and project_codex_root and project_codex_root.exists():
        pstate = project_codex_root / "state_5.sqlite"
        psessions = project_codex_root / "sessions"
        if pstate.exists() and psessions.is_dir():
            doc, streams, events, children = analyze_codex(
                workspace, str(pstate), str(psessions), roots)
            if doc["present"]:
                doc["source"] = "project"
                codex_docs.append(doc)
                codex_children |= children
                for tid, s in streams.items():
                    codex_run_total += s.cumulative_final or 0
                all_events.extend(events)
                for tid, s in streams.items():
                    token_events.extend(
                        (ts, _norm_codex_usage(u), tid) for ts, u in s.token_events)
                    tool_events.extend((ts, name, tid) for ts, name in s.tool_events)
                    turn_events.extend((ts, kind) for ts, kind in s.turn_events)
                    entity_events.extend((ts, tid) for ts, _k, _i in s.events)
                    streams_settings[tid] = s.settings
    if use_system_oc and opencode_db and Path(opencode_db).exists():
        doc, events, toks, tools_ = analyze_opencode(opencode_db, workspace)
        if doc["present"]:
            doc["source"] = "system"
            oc_docs.append(doc)
            all_events.extend(events)
            token_events.extend(toks)
            tool_events.extend(tools_)
            entity_events.extend(
                (ts, eid) for ts, _h, eid, _k, _i in events)
            for s in doc["sessions"]:
                sid = s["session_id"]
                per_entity[sid] = {
                    "kind": "opencode_session",
                    "is_subagent": bool(s.get("parent_id")),
                    "agent": s.get("agent"),
                    "model": s.get("model"),
                    "variant": s.get("variant"),
                    "tokens_input": s.get("tokens_input", 0),
                    "tokens_total_session": (
                        s.get("tokens_input", 0) + s.get("tokens_cache_read", 0)
                        + s.get("tokens_output", 0) + s.get("tokens_reasoning", 0)),
                    "started_at": s.get("started_at"),
                    "ended_at": s.get("ended_at"),
                }
    if use_project_oc and project_opencode_db and project_opencode_db.exists():
        doc, events, toks, tools_ = analyze_opencode(
            str(project_opencode_db), workspace)
        if doc["present"]:
            doc["source"] = "project"
            oc_docs.append(doc)
            all_events.extend(events)
            token_events.extend(toks)
            tool_events.extend(tools_)
            entity_events.extend(
                (ts, eid) for ts, _h, eid, _k, _i in events)

    codex_doc = codex_docs[0] if codex_docs else {"present": False,
                                                   "thread_count": 0}
    if len(codex_docs) > 1:
        codex_doc["sources_used"] = [d["source"] for d in codex_docs]
    oc_doc = oc_docs[0] if oc_docs else {"present": False, "session_count": 0}
    if len(oc_docs) > 1:
        oc_doc["sources_used"] = [d["source"] for d in oc_docs]

    opencode_run_total = None
    accounting_notes = []
    if oc_doc.get("present"):
        opencode_run_total = sum(
            (s.get("tokens_input", 0) + s.get("tokens_cache_read", 0)
             + s.get("tokens_output", 0) + s.get("tokens_reasoning", 0))
            for s in oc_doc.get("sessions", []))
        accounting_notes.append(
            "opencode: session token columns are per-session (subagents have "
            "their own sessions); total_from_all_sessions sums them")
    if codex_doc.get("present"):
        accounting_notes.append(
            "codex: each selected thread has its own cumulative usage counter; "
            "run totals sum the selected root and its subagent threads. Bucket "
            "totals are per-event "
            "total_token_usage deltas (cache-hit history is per submission; "
            "duplicate streaming ticks are excluded).")

    timeline = merged_timeline(all_events)
    intervals, gaps = active_intervals(timeline, idle_gap_seconds)
    window_start = timeline[0][0] if timeline else None
    window_end = timeline[-1][0] if timeline else None
    buckets = build_buckets(window_start, window_end, bucket_seconds,
                            intervals, token_events, tool_events, turn_events,
                            entity_events)
    waits = permission_wait_candidates(timeline, gaps, idle_gap_seconds,
                                       streams_settings, tool_events)
    whole = whole_stats(buckets, token_events, tool_events, turn_events,
                        intervals, per_entity, codex_run_total or None,
                        opencode_run_total, accounting_notes)
    return {
        "codex": codex_doc,
        "opencode": oc_doc,
        "analysis": {
            "bucket_seconds": bucket_seconds,
            "idle_gap_seconds": idle_gap_seconds,
            "window": {
                "start": window_start.isoformat() if window_start else None,
                "end": window_end.isoformat() if window_end else None,
            },
            "idle_exclusion": {
                "method": "merged timeline over main thread/session and all "
                          "subagents; gaps with no events anywhere exceed the "
                          "threshold and are excluded",
                "gap_count": len(gaps),
                "gaps": gaps,
                "idle_seconds_total": round(
                    sum(g["seconds"] for g in gaps), 1),
            },
            "permission_waits": waits,
            "buckets": buckets,
            "whole_session_stats": whole,
        },
    }


def discover(workspace: str, state_db: str, sessions_root: str,
             opencode_db: str, project_codex_root: Path,
             project_opencode_db: Path) -> dict:
    """List candidate session sources (system vs project-isolated)."""
    def _count_codex(db, sroot):
        if not Path(db).exists():
            return 0
        try:
            threads = cd.load_threads(db)
        except Exception:
            return 0
        return len(cd.select_threads(threads, workspace))

    def _count_opencode(db):
        if not Path(db).exists():
            return 0
        try:
            return len(opencode_sessions(db, workspace))
        except Exception:
            return 0

    sources = [
        {"id": "system_codex", "kind": "system", "harness": "codex",
         "root": state_db, "reachable": Path(state_db).exists(),
         "sessions_found": _count_codex(state_db, sessions_root),
         "note": "user-level ~/.codex telemetry"},
        {"id": "system_opencode", "kind": "system", "harness": "opencode",
         "root": opencode_db, "reachable": Path(opencode_db).exists(),
         "sessions_found": _count_opencode(opencode_db),
         "note": "user-level ~/.local/share/opencode/opencode.db"},
        {"id": "project_codex", "kind": "project", "harness": "codex",
         "root": str(project_codex_root), "reachable": project_codex_root.exists(),
         "sessions_found": _count_codex(
             project_codex_root / "state_5.sqlite",
             project_codex_root / "sessions"),
         "note": "workspace-bundled .sessions/codex copy"},
        {"id": "project_opencode", "kind": "project", "harness": "opencode",
         "root": str(project_opencode_db), "reachable": project_opencode_db.exists(),
         "sessions_found": _count_opencode(project_opencode_db),
         "note": "workspace-bundled .sessions/opencode-data copy"},
    ]
    return {"sources": sources}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Discover + analyze sessions for a run")
    ap.add_argument("--workspace", required=True)
    defaults = cd.default_paths()
    home = Path.home()
    ap.add_argument("--state-db", default=str(defaults["state_db"]))
    ap.add_argument("--sessions-root", default=str(defaults["sessions_root"]))
    ap.add_argument("--opencode-db",
                    default=str(home / ".local" / "share" / "opencode" / "opencode.db"))
    ap.add_argument("--project-codex-root", default=None,
                    help="<workspace>/.sessions/codex by default")
    ap.add_argument("--project-opencode-db", default=None,
                    help="<workspace>/.sessions/opencode-data/opencode.db by default")
    ap.add_argument("--session-source", choices=("system", "project", "all"),
                    default="system",
                    help="which session class to analyze (default system; "
                         "project = workspace-bundled .sessions copies)")
    ap.add_argument("--session-answers", default=None,
                    help="JSON answers from the evaluation agent for discovery "
                         "questions (e.g. {\"session_source\": \"system\"})")
    ap.add_argument("--bucket-seconds", type=int, default=DEFAULT_BUCKET_SECONDS)
    ap.add_argument("--idle-gap-seconds", type=int, default=600)
    ap.add_argument("--roots", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)

    ws = Path(args.workspace).resolve()
    eval_root = Path(__file__).resolve().parents[2]
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / ws.name / "sessions.json")
    project_codex_root = Path(args.project_codex_root) if args.project_codex_root \
        else ws / ".sessions" / "codex"
    project_opencode_db = Path(args.project_opencode_db) if args.project_opencode_db \
        else ws / ".sessions" / "opencode-data" / "opencode.db"

    disc = discover(str(ws), args.state_db, args.sessions_root,
                    args.opencode_db, project_codex_root, project_opencode_db)
    questions = []
    source_choice = args.session_source
    if args.session_answers and Path(args.session_answers).exists():
        try:
            answers = json.loads(Path(args.session_answers).read_text())
            if isinstance(answers, dict) and answers.get("session_source") in (
                    "system", "project", "all"):
                source_choice = answers["session_source"]
        except (OSError, json.JSONDecodeError):
            pass
    both_have = [s for s in disc["sources"]
                 if s["sessions_found"] and s["kind"] == "system"]
    if args.session_source == "system" and len(both_have) > 1 and not args.session_answers:
        questions.append({
            "id": "session_source",
            "question": "Both codex and opencode system-level sessions exist "
                        "for this workspace. Which harness ran the contestant?",
            "reason": "session discovery found multiple harnesses; the agent "
                      "must classify the run",
            "suggested_source": "workspace AGENTS.md branch name or the run "
                                "notes (oc*/omo* branches use opencode)",
            "answer": None,
        })

    env_path = ws / ".eval" / "env_snapshot.json"
    env_doc = {
        "captured": env_path.exists(),
        "path": str(env_path) if env_path.exists() else None,
        "sha256": hashlib.sha256(env_path.read_bytes()).hexdigest()
        if env_path.exists() else None,
    }

    result = analyze(str(ws), args.state_db, args.sessions_root,
                     args.opencode_db, args.bucket_seconds,
                     args.idle_gap_seconds, source_choice,
                     cd.parse_roots(args.roots),
                     project_codex_root, project_opencode_db)
    result["workspace"] = str(ws)
    result["discovered_at"] = datetime.now(timezone.utc).isoformat()
    result["sources"] = disc["sources"]
    result["selected_source"] = source_choice
    result["selection"] = {
        "method": "script_discovery + agent classification",
        "roots": cd.parse_roots(args.roots),
        "questions": questions,
        "notes": [
            "the evaluation agent classifies which sessions belong to the "
            "contestant run; unextractable choices are surfaced as questions",
        ],
    }
    result["env_snapshot"] = env_doc
    result["provenance"] = {
        "state_db": args.state_db,
        "sessions_root": args.sessions_root,
        "opencode_db": args.opencode_db,
        "session_source": source_choice,
        "bucket_seconds": args.bucket_seconds,
        "idle_gap_seconds": args.idle_gap_seconds,
        "generated_at": datetime.now(timezone.utc).isoformat(),
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2) + "\n")

    codex = result["codex"]
    oc = result["opencode"]
    an = result["analysis"]
    print(f"wrote {out_path}")
    print(f"source={source_choice} codex_threads={codex.get('thread_count', 0)} "
          f"opencode_sessions={oc.get('session_count', 0)}")
    print(f"window={an['window']['start']} -> {an['window']['end']}")
    print(f"buckets={len(an['buckets'])} "
          f"idle_gaps={an['idle_exclusion']['gap_count']} "
          f"(idle {an['idle_exclusion']['idle_seconds_total']:.0f}s) "
          f"permission_wait_candidates={an['permission_waits']['candidate_count']}")
    ws_ = an["whole_session_stats"]
    print(f"whole: tokens={ws_['tokens']['total']:,} "
          f"cache_hit={ws_['cache']['hit_ratio']} "
          f"tools={ws_['tools']['total']} "
          f"active={ws_['activity']['active_seconds']:.0f}s")
    for q in questions:
        print(f"QUESTION: {q['id']}: {q['question']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

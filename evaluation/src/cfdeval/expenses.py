#!/usr/bin/env python3
"""Extract execution expenses (time, tokens, estimated cost) for a codex
benchmark contestant run.

Usage:
  python3 evaluation/tools/extract_expenses.py --workspace <contestant-workspace>
    [--state-db PATH] [--goals-db PATH] [--logs-db PATH]
    [--sessions-root PATH] [--cost-metadata PATH] [--out PATH]

Sources (spec: evaluation/specs/expenses_spec.md):
  - time:    goals_1.sqlite thread_goals.time_used_seconds (goal time, direct)
  - tokens:  logs_2.sqlite per-turn codex.turn.token_usage.*; fallback
             state_5.sqlite threads.tokens_used
  - cost:    evaluation/config/cost_metadata.json * tokens
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

from cfdeval import claude_data
from cfdeval import codex_data as cd


def model_cost(meta: dict, model: str | None):
    """Return ``(prices, used_defaults)`` for an exact/alias model.

    Unknown and explicitly unresolved models stay unpriced.  Falling back to
    the generic defaults would make a missing contract price look like a real
    provider rate in the dashboard.
    """
    defaults = meta.get("defaults", {})
    if not model:
        return None, True
    models = meta.get("models", {})
    key = model.lower()
    entry = models.get(key)
    if entry is None:
        suffixes = [candidate for candidate in models
                    if "/" not in candidate and key.endswith(candidate)]
        if len(suffixes) == 1:
            entry = models[suffixes[0]]
    if entry is None:
        return None, True
    if entry.get("pricing_status") == "unresolved":
        return None, False
    input_rate = entry.get("input_per_mtok", defaults["input_per_mtok"])
    return {
        "input_per_mtok": input_rate,
        "cached_input_per_mtok": entry.get("cached_input_per_mtok", defaults["cached_input_per_mtok"]),
        "cache_write_per_mtok": entry.get("cache_write_per_mtok", input_rate),
        "output_per_mtok": entry.get("output_per_mtok", defaults["output_per_mtok"]),
        "input_share": defaults["input_share"],
        "note": entry.get("note"),
    }, False


def cost_for(price: dict, *, input_t=None, cached_t=None, cache_write_t=None,
             output_t=None, total_t=None) -> float:
    """Estimate USD for a token bundle. Exact splits when available, else
    blended fallback using input_share."""
    if total_t is not None and input_t is None and output_t is None:
        share = price["input_share"]
        return total_t / 1e6 * (share * price["input_per_mtok"] + (1 - share) * price["output_per_mtok"])
    non_cached = max(
        (input_t or 0) - (cached_t or 0) - (cache_write_t or 0), 0)
    return (
        non_cached * price["input_per_mtok"]
        + (cached_t or 0) * price["cached_input_per_mtok"]
        + (cache_write_t or 0) * price["cache_write_per_mtok"]
        + (output_t or 0) * price["output_per_mtok"]
    ) / 1e6


def estimate_by_model(meta: dict, by_model: dict) -> dict:
    """Price immutable token aggregates using the supplied current metadata."""
    estimates = {}
    total_cost = 0.0
    unpriced_tokens = 0
    for model, agg in by_model.items():
        price, used_defaults = model_cost(meta, model)
        if price is None:
            unpriced_tokens += agg.get("total", 0)
            cost = None
        else:
            cost = cost_for(
                price,
                input_t=agg.get("input") or None,
                cached_t=agg.get("cached_input") or None,
                cache_write_t=agg.get("cache_write") or None,
                output_t=(
                    (agg.get("output", 0) or 0)
                    + (0 if agg.get("reasoning_is_output_subset", True)
                       else (agg.get("reasoning_output", 0) or 0))
                ) or None,
                total_t=agg.get("total") if not agg.get("input") and not agg.get("output") else None,
            )
        estimates[model] = {
            "usd": round(cost, 4) if cost is not None else None,
            "tokens": agg.get("total", 0),
            "pricing": "unresolved" if price is None else "metadata",
        }
        if cost is not None:
            total_cost += cost
    return {
        "total": round(total_cost, 4) if not unpriced_tokens else None,
        "by_model": estimates,
        "unpriced_tokens": unpriced_tokens,
        "estimate": True,
    }


def opencode_expense_facts(metadata: dict, workspace: str,
                           cost_metadata_path: str | Path,
                           roots: list[str] | None = None) -> dict:
    """Build a durable expense sidecar from root-scoped OpenCode metadata."""
    sessions = ((metadata.get("opencode") or {}).get("sessions") or [])
    roots = roots or (metadata.get("provenance") or {}).get("selected_roots")
    if not roots:
        candidates = [s.get("session_id") for s in sessions
                      if s.get("session_id") and not s.get("parent_id")]
        if len(candidates) == 1:
            roots = candidates
        elif sessions:
            raise ValueError(
                "OpenCode expense attribution requires explicit selected roots")
    sessions = cd.select_opencode_session_trees(sessions, roots)
    by_model: dict[str, dict] = {}
    by_thread: dict[str, dict] = {}
    main_tokens = subagent_tokens = 0
    provider_cost = 0.0
    for session in sessions:
        units = session.get("usage_by_model") or [{
            "model": session.get("model"), "provider": session.get("provider"),
            "variant": session.get("variant"),
            "tokens_input": session.get("tokens_input", 0),
            "tokens_cache_read": session.get("tokens_cache_read", 0),
            "tokens_cache_write": session.get("tokens_cache_write", 0),
            "tokens_output": session.get("tokens_output", 0),
            "tokens_reasoning": session.get("tokens_reasoning", 0),
            "cost": session.get("cost", 0),
        }]
        thread_bundles: dict[str, dict] = {}
        thread_total = 0
        thread_provider_cost = 0.0
        for unit in units:
            model = unit.get("model") or "unknown"
            provider = unit.get("provider")
            key = f"{provider}/{model}" if provider else model
            raw_input = int(unit.get("tokens_input", 0) or 0)
            cache_read = int(unit.get("tokens_cache_read", 0) or 0)
            cache_write = int(unit.get("tokens_cache_write", 0) or 0)
            output = int(unit.get("tokens_output", 0) or 0)
            reasoning = int(unit.get("tokens_reasoning", 0) or 0)
            input_total = raw_input + cache_read + cache_write
            total = input_total + output + reasoning
            bundle = {
                "input": input_total,
                "cached_input": cache_read,
                "cache_write": cache_write,
                "non_cached_input": raw_input + cache_write,
                "output": output,
                "reasoning_output": reasoning,
                "reasoning_is_output_subset": False,
                "total": total,
            }
            agg = by_model.setdefault(key, {name: 0 for name in (
                "input", "cached_input", "cache_write", "non_cached_input", "output",
                "reasoning_output", "total")})
            agg["reasoning_is_output_subset"] = False
            thread_agg = thread_bundles.setdefault(key, {name: 0 for name in (
                "input", "cached_input", "cache_write", "non_cached_input", "output",
                "reasoning_output", "total")})
            thread_agg["reasoning_is_output_subset"] = False
            for name in ("input", "cached_input", "cache_write", "non_cached_input",
                         "output", "reasoning_output", "total"):
                agg[name] += bundle[name]
                thread_agg[name] += bundle[name]
            thread_total += total
            thread_provider_cost += float(unit.get("cost", 0) or 0)
        sid = session.get("session_id") or "unknown"
        by_thread[sid] = {
            "model": session.get("entry_model") or session.get("model"),
            "provider": session.get("entry_provider") or session.get("provider"),
            "variant": session.get("entry_variant") or session.get("variant"),
            "is_subagent": bool(session.get("parent_id")),
            "source": ("opencode_assistant_messages"
                       if session.get("usage_by_model") else "opencode_session"),
            "tokens": thread_bundles,
            "total": thread_total,
        }
        if session.get("parent_id"):
            subagent_tokens += thread_total
        else:
            main_tokens += thread_total
        provider_cost += thread_provider_cost
    price_path = Path(cost_metadata_path)
    price_meta = json.loads(price_path.read_text())
    estimate = estimate_by_model(price_meta, by_model)
    estimate.update({
        "metadata": str(price_path),
        "provider_reported_total": round(provider_cost, 6),
    })
    sw = metadata.get("session_window") or {}
    return {
        "note": "OpenCode selected-session-tree token facts; current manager price estimate is separate from provider-reported cost",
        "workspace": workspace,
        "time_seconds": {
            "goal_time": 0,
            "wall_time": 0,
            "activity_time_seconds": sw.get("activity_time_seconds", 0),
            "idle_time_seconds": sw.get("idle_time_seconds", 0),
            "idle_gap_threshold_seconds": sw.get("idle_gap_threshold_seconds", 600),
            "started_at": sw.get("started_at"),
            "ended_at": sw.get("ended_at"),
        },
        "tokens": {
            "total": main_tokens + subagent_tokens,
            "by_model": by_model,
            "by_thread": by_thread,
            "main_vs_subagent": {"main": main_tokens, "subagent": subagent_tokens},
        },
        "cost_estimate_usd": estimate,
        "provenance": {
            "source": "workspace-local OpenCode metadata selected session tree",
            "selected_roots": roots,
            "provider_reported_total": round(provider_cost, 6),
        },
    }


def claude_expense_facts(workspace: str, roots: list[str],
                         claude_root: str | Path,
                         cost_metadata_path: str | Path) -> dict:
    """Build expenses from additive assistant-message usage in Claude JSONL."""
    data = claude_data.facts(workspace, roots, claude_root)
    by_thread = {}
    main_tokens = subagent_tokens = 0
    for entity in data["entities"]:
        eid = entity["entity_id"]
        usage = data["usage_by_entity"].get(eid, {})
        total = sum(bundle["total"] for bundle in usage.values())
        by_thread[eid] = {
            "model": max(usage, key=lambda model: usage[model]["total"])
            if usage else None,
            "is_subagent": entity["is_subagent"],
            "source": "claude_assistant_messages",
            "tokens": usage,
            "usage_by_model_effort": data["usage_attribution_by_entity"].get(
                eid, []),
            "total": total,
        }
        if entity["is_subagent"]:
            subagent_tokens += total
        else:
            main_tokens += total
    stamps = sorted(ts for ts, _h, _eid, _kind, _info in data["all_events"])
    started = stamps[0] if stamps else None
    ended = stamps[-1] if stamps else None
    wall = (ended - started).total_seconds() if started and ended else 0.0
    price_path = Path(cost_metadata_path)
    estimate = estimate_by_model(json.loads(price_path.read_text()), data["by_model"])
    estimate["metadata"] = str(price_path)
    return {
        "note": "Claude selected-root token facts from workspace-local project JSONL; "
                "unknown models remain unpriced",
        "workspace": workspace,
        "time_seconds": {
            "goal_time": None,
            "wall_time": round(wall, 1),
            "started_at": started.isoformat() if started else None,
            "ended_at": ended.isoformat() if ended else None,
            "by_root_tree": {
                root: {
                    "status": None,
                    "model": None,
                    "threads": sum(e["root_id"] == root for e in data["entities"]),
                    "tokens_used": sum(
                        sum(v["total"] for v in data["usage_by_entity"].get(
                            e["entity_id"], {}).values())
                        for e in data["entities"] if e["root_id"] == root),
                    "goal_time_seconds": None,
                }
                for root in roots
            },
        },
        "tokens": {
            "total": main_tokens + subagent_tokens,
            "by_model": data["by_model"],
            "by_thread": by_thread,
            "main_vs_subagent": {"main": main_tokens, "subagent": subagent_tokens},
            "fallback_tokens": 0,
            "discrepancy_notes": [],
            "accounting_note": "Anthropic input categories are disjoint in the "
                               "persisted usage object; manager input is their sum",
        },
        "cost_estimate_usd": estimate,
        "provenance": {
            "source": "workspace-local Claude project JSONL",
            "claude_root": str(claude_root),
            "selected_roots": roots,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "num_threads": len(data["entities"]),
        },
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Extract codex contestant expenses")
    ap.add_argument("--workspace", required=True)
    ap.add_argument("--state-db", default=None)
    ap.add_argument("--goals-db", default=None)
    ap.add_argument("--logs-db", default=None)
    ap.add_argument("--sessions-root", default=None)
    ap.add_argument("--harness", choices=("codex", "claude"), default="codex")
    ap.add_argument("--claude-root", default=None,
                    help="<workspace>/.sessions/claude by default")
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--cost-metadata", default=str(root / "config" / "cost_metadata.json"))
    ap.add_argument("--out", default=None)
    ap.add_argument(
        "--roots",
        default=None,
        help="Comma-separated root thread ids to include (each with its subagent "
             "tree). Default: all sessions whose cwd is inside the workspace, "
             "including botched/paused/blocked ones.",
    )
    args = ap.parse_args(argv)

    workspace = str(Path(args.workspace).resolve())
    paths = cd.local_telemetry_paths(
        workspace, state_db=args.state_db, goals_db=args.goals_db,
        logs_db=args.logs_db, sessions_root=args.sessions_root,
        claude_root=args.claude_root)
    for key in ("state_db", "goals_db", "logs_db", "sessions_root"):
        setattr(args, key, str(paths[key]))
    eval_root = Path(__file__).resolve().parents[2]
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / Path(workspace).name / "expenses.json"
    )

    if args.harness == "claude":
        roots = cd.parse_roots(args.roots)
        if not roots:
            print("ERROR: Claude expense extraction requires explicit --roots",
                  file=sys.stderr)
            return 2
        try:
            expenses = claude_expense_facts(
                workspace, roots, paths["claude_root"], args.cost_metadata)
        except ValueError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 2
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(expenses, indent=2) + "\n")
        print(f"wrote {out_path}")
        print(f"threads={len(expenses['tokens']['by_thread'])} "
              f"tokens={expenses['tokens']['total']:,} cost=unavailable"
              if expenses["cost_estimate_usd"]["total"] is None
              else f"cost≈${expenses['cost_estimate_usd']['total']:.2f}")
        return 0

    threads = cd.load_threads(args.state_db)
    cd.rebase_rollout_paths(threads, args.sessions_root)
    edges = cd.load_spawn_edges(args.state_db)
    parent_by_child = {child: parent for parent, child in edges}
    goals = cd.load_goals(args.goals_db) if Path(args.goals_db).is_file() else {}
    selected = cd.select_threads(threads, workspace)
    roots, all_ids = cd.thread_trees(selected, edges)
    children = {c for _, c in edges}
    full_threads = {tid: threads.get(tid) for tid in all_ids}
    requested_roots = cd.parse_roots(args.roots)
    if requested_roots is not None:
        missing = [r for r in requested_roots if r not in threads]
        if missing:
            print(f"ERROR: unknown root thread ids: {missing}", file=sys.stderr)
            return 2
        all_ids = set()
        for r in requested_roots:
            all_ids |= cd.tree_of(r, threads, edges)
        roots = [r for r in requested_roots]
        full_threads = {tid: threads.get(tid) for tid in all_ids}
    else:
        # Recompute roots among the full tree (roots of the complete trees).
        roots = [tid for tid in all_ids if tid not in children and full_threads.get(tid)]

    # Per-root-tree summary (so botched/abandoned sessions stay visible and
    # separable from the real run).
    by_root_tree = {}
    for rid in roots:
        tree_ids = cd.tree_of(rid, threads, edges)
        state_tree_tokens = sum(
            (threads.get(tid) or {}).get("tokens_used", 0) for tid in tree_ids
        )
        g = goals.get(rid, {})
        by_root_tree[rid] = {
            "status": g.get("status"),
            "goal_time_seconds": g.get("time_used_seconds", 0),
            "goal_tokens": g.get("tokens_used", 0),
            "threads": len(tree_ids),
            "tokens_used": None,
            "state_tokens_inherited_inclusive": state_tree_tokens,
            "model": (full_threads.get(rid) or {}).get("model"),
        }

    usage = cd.load_turn_usage(args.logs_db, all_ids) if Path(args.logs_db).is_file() else {}
    meta = json.loads(Path(args.cost_metadata).read_text())
    parent_task_ids = {
        tid: cd.rollout_task_started_ids((threads.get(tid) or {}).get("rollout_path"))
        for tid in set(parent_by_child.values()) if threads.get(tid)
    }

    # Rollouts retain exact cached/input/output splits even when legacy
    # logs_2.sqlite lacks per-turn usage rows.
    cumulative = {
        tid: cd.rollout_usage_facts(
            (full_threads.get(tid) or {}).get("rollout_path"),
            (threads.get(parent_by_child.get(tid)) or {}).get("rollout_path"),
            parent_task_ids.get(parent_by_child.get(tid)),
            (full_threads.get(tid) or {}).get("model"),
        )
        for tid in all_ids if full_threads.get(tid)
    }

    # ---- tokens ----------------------------------------------------------
    by_thread = {}
    by_model = {}
    fallback_tokens_total = 0
    discrepancy_notes = []
    main_tokens = 0
    sub_tokens = 0

    for tid in sorted(all_ids):
        t = full_threads[tid]
        if t is None:
            continue
        is_sub = tid in children
        recs = usage.get(tid, [])
        rollout_rec = cumulative.get(tid) or {}
        accounting = rollout_rec.get("accounting") or {}
        if accounting.get("available"):
            thread_model = t["model"]
            declared = t["tokens_used"]
            observed = rollout_rec["total_tokens"]
            is_fork = bool(accounting.get("is_fork"))
            scale = (declared / observed
                     if not is_fork and declared and observed else 1.0)
            accounted_total = declared if not is_fork and declared else observed
            rollout_by_model = rollout_rec.get("usage_by_model") or {
                thread_model: rollout_rec}
            per_model = {}
            for model, model_usage in rollout_by_model.items():
                per_model[model] = {
                    "input": int(round(model_usage["input_tokens"] * scale)),
                    "cached": int(round(model_usage["cached_input_tokens"] * scale)),
                    "non_cached": int(round(
                        model_usage["non_cached_input_tokens"] * scale)),
                    "output": int(round(model_usage["output_tokens"] * scale)),
                    "reasoning_output": int(round(
                        model_usage["reasoning_output_tokens"] * scale)),
                    "total": int(round(model_usage["total_tokens"] * scale)),
                }
            # Preserve the authoritative standalone state total exactly after
            # per-model rounding.  Fork totals are already owned-rollout exact.
            total_delta = accounted_total - sum(
                bundle["total"] for bundle in per_model.values())
            if total_delta and per_model:
                largest = max(
                    per_model,
                    key=lambda model: rollout_by_model[model]["total_tokens"])
                per_model[largest]["total"] += total_delta
            total = accounted_total
            thread_entry = {
                "model": thread_model,
                "is_subagent": is_sub,
                "source": ("rollout_fork_delta" if is_fork else
                           "rollout_scaled_to_threads" if scale != 1.0 else
                           "rollout_cumulative"),
                "tokens": per_model,
                "total": total,
                "inherited_baseline_tokens": (
                    (accounting.get("baseline") or {}).get("total_tokens", 0)),
                "fallback_model_tokens": rollout_rec.get(
                    "fallback_model_tokens", 0),
            }
            if is_fork and declared != observed:
                discrepancy_notes.append(
                    f"thread {tid}: ignored inherited-inclusive state token "
                    f"counter {declared}; rollout-owned usage is {observed}"
                )
            elif scale != 1.0:
                discrepancy_notes.append(
                    f"root thread {tid}: rollout total {observed} scaled to "
                    f"standalone state token counter {declared} (x{scale:.3f})"
                )
        elif recs:
            thread_model = t["model"]
            per_model_log = {}
            log_total = 0
            for rec in recs:
                m = rec["model"] or thread_model
                agg = per_model_log.setdefault(
                    m,
                    {"input": 0, "cached": 0, "non_cached": 0, "output": 0, "reasoning_output": 0, "total": 0},
                )
                agg["input"] += rec["input_tokens"]
                agg["cached"] += rec["cached_input_tokens"]
                agg["non_cached"] += rec["non_cached_input_tokens"]
                agg["output"] += rec["output_tokens"]
                agg["reasoning_output"] += rec["reasoning_output_tokens"]
                agg["total"] += rec["total_tokens"]
                log_total += rec["total_tokens"]
            declared = t["tokens_used"]
            # logs may only cover a window; threads.tokens_used is codex's own
            # authoritative per-thread total, so scale the observed splits.
            if declared and abs(declared - log_total) / max(declared, 1) > 0.05:
                source = "logs_scaled_to_threads"
                scale = declared / log_total if log_total else 1.0
                discrepancy_notes.append(
                    f"thread {tid}: usage-log total {log_total} scaled to "
                    f"threads.tokens_used {declared} (x{scale:.3f})"
                )
            else:
                source = "logs"
                scale = 1.0
            per_model = {}
            total = declared or log_total
            for m, agg in per_model_log.items():
                per_model[m] = {
                    "input": int(round(agg["input"] * scale)),
                    "cached": int(round(agg["cached"] * scale)),
                    "non_cached": int(round(agg["non_cached"] * scale)),
                    "output": int(round(agg["output"] * scale)),
                    "reasoning_output": int(round(agg["reasoning_output"] * scale)),
                    "total": int(round(agg["total"] * scale)),
                }
            thread_entry = {
                "model": thread_model,
                "is_subagent": is_sub,
                "source": source,
                "tokens": per_model,
                "total": total,
            }
        else:
            total = t["tokens_used"]
            m = t["model"]
            thread_entry = {
                "model": m,
                "is_subagent": is_sub,
                "source": "threads_fallback",
                "tokens": {m: {"input": 0, "cached": 0, "non_cached": 0, "output": 0,
                               "reasoning_output": 0, "total": total}},
                "total": total,
                "fallback_tokens": total,
            }
            fallback_tokens_total += total
        by_thread[tid] = thread_entry
        for m, agg in thread_entry["tokens"].items():
            m = m or "unknown"
            bm = by_model.setdefault(
                m,
                {"input": 0, "cached_input": 0, "non_cached_input": 0,
                 "output": 0, "reasoning_output": 0, "total": 0, "fallback_tokens": 0},
            )
            bm["input"] += agg["input"]
            bm["cached_input"] += agg["cached"]
            bm["non_cached_input"] += agg["non_cached"]
            bm["output"] += agg["output"]
            bm["reasoning_output"] += agg["reasoning_output"]
            bm["total"] += agg["total"]
            if "fallback_tokens" in thread_entry:
                bm["fallback_tokens"] += thread_entry["fallback_tokens"]
        if is_sub:
            sub_tokens += total
        else:
            main_tokens += total

    total_tokens = sum(b["total"] for b in by_model.values())
    for rid, root_info in by_root_tree.items():
        tree_ids = cd.tree_of(rid, threads, edges)
        root_info["tokens_used"] = sum(
            (by_thread.get(tid) or {}).get("total", 0) for tid in tree_ids)

    # ---- cost ------------------------------------------------------------
    current_cost = estimate_by_model(meta, by_model)

    # ---- time ------------------------------------------------------------
    goal_by_root = {}
    goal_time = 0.0
    for rid in roots:
        g = goals.get(rid)
        if g:
            goal_by_root[rid] = {
                "status": g["status"],
                "time_used_seconds": g["time_used_seconds"],
                "tokens_used": g["tokens_used"],
            }
            goal_time += g["time_used_seconds"]

    starts, ends = [], []
    for tid in all_ids:
        t = full_threads.get(tid)
        if not t:
            continue
        s, e = cd.session_window(t["rollout_path"])
        if s and e:
            starts.append(s)
            ends.append(e)
        else:
            starts.append(datetime.fromtimestamp(t["created_at"], tz=timezone.utc))
            ends.append(datetime.fromtimestamp(t["updated_at"], tz=timezone.utc))
    wall_time = (max(ends) - min(starts)).total_seconds() if starts else 0.0

    expenses = {
        "workspace": workspace,
        "time_seconds": {
            "goal_time": round(goal_time, 1),
            "wall_time": round(wall_time, 1),
            "started_at": min(starts).isoformat() if starts else None,
            "ended_at": max(ends).isoformat() if ends else None,
            "by_root_thread": goal_by_root,
            "by_root_tree": by_root_tree,
        },
        "tokens": {
            "total": total_tokens,
            "by_model": by_model,
            "by_thread": by_thread,
            "main_vs_subagent": {"main": main_tokens, "subagent": sub_tokens},
            "fallback_tokens": fallback_tokens_total,
            "discrepancy_notes": discrepancy_notes,
            "accounting_note": (
                "Cached/input/output splits come from thread-owned rollout "
                "counter deltas. Fork replay is excluded at the first child-owned "
                "task boundary. Each delta is attributed to the most recent owned "
                "turn_context or thread_settings_applied model; only deltas before "
                "an explicit owned model event use the thread database model. "
                "Fork state counters are diagnostic only. "
                "A standalone root may retain the legacy state-counter scaling "
                "fallback when its rollout ends before the persisted root total."
            ),
        },
        "cost_estimate_usd": {
            **current_cost,
            "metadata": args.cost_metadata,
        },
        "provenance": {
            "state_db": args.state_db,
            "goals_db": args.goals_db,
            "logs_db": args.logs_db,
            "sessions_root": args.sessions_root,
            "fetched_at": datetime.now(timezone.utc).isoformat(),
            "num_threads": len(all_ids),
            "num_roots": len(roots),
        },
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(expenses, indent=2) + "\n")
    print(f"wrote {out_path}")
    cost_text = (f"${current_cost['total']:.2f}"
                 if current_cost["total"] is not None else "unavailable")
    print(
        f"threads={len(all_ids)} roots={len(roots)} tokens={total_tokens:,} "
        f"goal_time={goal_time:.0f}s wall_time={wall_time:.0f}s "
        f"cost≈{cost_text}"
    )
    for rid, info in by_root_tree.items():
        print(
            f"  root {rid[:8]} [{info['status']}] threads={info['threads']} "
            f"tokens={info['tokens_used']:,} goal_time={info['goal_time_seconds']}s"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

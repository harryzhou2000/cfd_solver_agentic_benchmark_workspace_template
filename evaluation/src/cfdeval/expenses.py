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

from cfdeval import codex_data as cd


def model_cost(meta: dict, model: str | None):
    """Return (prices dict or None, used_defaults: bool)."""
    defaults = meta.get("defaults", {})
    if not model:
        return defaults, True
    entry = meta.get("models", {}).get(model.lower())
    if entry is None:
        return defaults, True
    return {
        "input_per_mtok": entry.get("input_per_mtok", defaults["input_per_mtok"]),
        "cached_input_per_mtok": entry.get("cached_input_per_mtok", defaults["cached_input_per_mtok"]),
        "output_per_mtok": entry.get("output_per_mtok", defaults["output_per_mtok"]),
        "input_share": defaults["input_share"],
        "note": entry.get("note"),
    }, False


def cost_for(price: dict, *, input_t=None, cached_t=None, output_t=None, total_t=None) -> float:
    """Estimate USD for a token bundle. Exact splits when available, else
    blended fallback using input_share."""
    if total_t is not None and input_t is None and output_t is None:
        share = price["input_share"]
        return total_t / 1e6 * (share * price["input_per_mtok"] + (1 - share) * price["output_per_mtok"])
    non_cached = (input_t or 0) - (cached_t or 0)
    return (
        non_cached * price["input_per_mtok"]
        + (cached_t or 0) * price["cached_input_per_mtok"]
        + (output_t or 0) * price["output_per_mtok"]
    ) / 1e6


def estimate_by_model(meta: dict, by_model: dict) -> dict:
    """Price immutable token aggregates using the supplied current metadata."""
    estimates = {}
    total_cost = 0.0
    unpriced_tokens = 0
    for model, agg in by_model.items():
        price, used_defaults = model_cost(meta, model)
        if used_defaults and meta.get("models", {}).get(model.lower()) is None:
            unpriced_tokens += agg.get("total", 0)
        cost = cost_for(
            price,
            input_t=agg.get("input") or None,
            cached_t=agg.get("cached_input") or None,
            output_t=agg.get("output") or None,
            total_t=agg.get("total") if not agg.get("input") and not agg.get("output") else None,
        )
        estimates[model] = {
            "usd": round(cost, 4),
            "tokens": agg.get("total", 0),
            "pricing": "defaults" if used_defaults else "metadata",
        }
        total_cost += cost
    return {
        "total": round(total_cost, 4),
        "by_model": estimates,
        "unpriced_tokens": unpriced_tokens,
        "estimate": True,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Extract codex contestant expenses")
    ap.add_argument("--workspace", required=True)
    defaults = cd.default_paths()
    ap.add_argument("--state-db", default=str(defaults["state_db"]))
    ap.add_argument("--goals-db", default=str(defaults["goals_db"]))
    ap.add_argument("--logs-db", default=str(defaults["logs_db"]))
    ap.add_argument("--sessions-root", default=str(defaults["sessions_root"]))
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
    eval_root = Path(__file__).resolve().parents[2]
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / Path(workspace).name / "expenses.json"
    )

    threads = cd.load_threads(args.state_db)
    edges = cd.load_spawn_edges(args.state_db)
    goals = cd.load_goals(args.goals_db)
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
        tree_tokens = sum(
            (threads.get(tid) or {}).get("tokens_used", 0) for tid in tree_ids
        )
        g = goals.get(rid, {})
        by_root_tree[rid] = {
            "status": g.get("status"),
            "goal_time_seconds": g.get("time_used_seconds", 0),
            "goal_tokens": g.get("tokens_used", 0),
            "threads": len(tree_ids),
            "tokens_used": tree_tokens,
            "model": (full_threads.get(rid) or {}).get("model"),
        }

    usage = cd.load_turn_usage(args.logs_db, all_ids)
    meta = json.loads(Path(args.cost_metadata).read_text())

    # Rollouts retain exact cached/input/output splits even when legacy
    # logs_2.sqlite lacks per-turn usage rows.
    cumulative = {
        tid: cd.rollout_usage_facts((full_threads.get(tid) or {}).get("rollout_path"))
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
        if rollout_rec.get("total_tokens", 0):
            thread_model = t["model"]
            declared = t["tokens_used"]
            observed = rollout_rec["total_tokens"]
            scale = declared / observed if declared and observed else 1.0
            per_model = {thread_model: {
                "input": int(round(rollout_rec["input_tokens"] * scale)),
                "cached": int(round(rollout_rec["cached_input_tokens"] * scale)),
                "non_cached": int(round(rollout_rec["non_cached_input_tokens"] * scale)),
                "output": int(round(rollout_rec["output_tokens"] * scale)),
                "reasoning_output": int(round(rollout_rec["reasoning_output_tokens"] * scale)),
                "total": declared or observed,
            }}
            total = declared or observed
            thread_entry = {
                "model": thread_model,
                "is_subagent": is_sub,
                "source": "rollout_cumulative" if scale == 1.0 else "rollout_scaled_to_threads",
                "tokens": per_model,
                "total": total,
            }
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
                "Cached/input/output splits come from each selected thread's "
                "terminal rollout counter and are scaled only when needed to "
                "match state_5.sqlite threads.tokens_used."
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
    print(
        f"threads={len(all_ids)} roots={len(roots)} tokens={total_tokens:,} "
        f"goal_time={goal_time:.0f}s wall_time={wall_time:.0f}s "
        f"cost≈${total_cost:.2f}"
    )
    for rid, info in by_root_tree.items():
        print(
            f"  root {rid[:8]} [{info['status']}] threads={info['threads']} "
            f"tokens={info['tokens_used']:,} goal_time={info['goal_time_seconds']}s"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

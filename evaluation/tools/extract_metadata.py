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
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import codex_data as cd  # noqa: E402


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


def main() -> int:
    ap = argparse.ArgumentParser(description="Extract contestant run metadata")
    ap.add_argument("--workspace", required=True)
    defaults = cd.default_paths()
    home = Path.home()
    ap.add_argument("--state-db", default=str(defaults["state_db"]))
    ap.add_argument("--goals-db", default=str(defaults["goals_db"]))
    ap.add_argument("--logs-db", default=str(defaults["logs_db"]))
    ap.add_argument("--sessions-root", default=str(defaults["sessions_root"]))
    ap.add_argument("--history", default=str(cd.codex_home() / "history.jsonl"))
    ap.add_argument("--ocx-config", default=str(home / ".opencodex" / "config.json"))
    ap.add_argument("--ocx-catalog",
                    default=str(cd.codex_home() / "opencodex-catalog.json"))
    ap.add_argument("--plugins-root", default=str(cd.codex_home() / "plugins"))
    ap.add_argument("--roots", default=None)
    eval_root = Path(__file__).resolve().parents[1]
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    workspace = str(Path(args.workspace).resolve())
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / Path(workspace).name / "metadata.json"
    )
    threads = cd.load_threads(args.state_db)
    edges = cd.load_spawn_edges(args.state_db)
    goals = cd.load_goals(args.goals_db)
    selected = cd.select_threads(threads, workspace)
    roots, all_ids = cd.thread_trees(selected, edges)
    children = {c for _, c in edges}
    requested = cd.parse_roots(args.roots)
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

    usage = cd.load_turn_usage(args.logs_db, all_ids)
    catalog = load_catalog(args.ocx_catalog)
    history = load_history(args.history)
    plugins = load_plugins(args.plugins_root)

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
    rt = cd.codex_home() / "codex-runtime.json"
    if rt.exists():
        try:
            harness["codex_runtime_version"] = json.loads(rt.read_text()).get("selectedVersion")
        except json.JSONDecodeError:
            pass

    # ---- threads: models / effort / context / subagents ------------------
    models: dict[str, dict] = {}
    context = {"by_model": {}, "by_thread": {}, "notes": []}
    subagents = []
    for tid in sorted(all_ids):
        t = threads.get(tid)
        if not t:
            continue
        recs = usage.get(tid, [])
        efforts = thread_efforts(t["rollout_path"], recs)
        max_input = max((r["input_tokens"] for r in recs), default=None)
        mean_input = (sum(r["input_tokens"] for r in recs) / len(recs)) if recs else None
        is_sub = tid in children
        entry = {
            "thread_id": tid,
            "is_subagent": is_sub,
            "model": t["model"],
            "model_provider": t["model_provider"],
            "reasoning_effort": efforts or None,
            "tokens_used": t["tokens_used"],
            "cwd": t["cwd"],
            "git_branch": t["git_branch"],
            "created_at": t["created_at"],
            "updated_at": t["updated_at"],
            "context_used_max_input": max_input,
            "context_used_mean_input": round(mean_input, 1) if mean_input is not None else None,
        }
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
        for e in efforts or []:
            if e not in models[m]["reasoning_efforts_seen"]:
                models[m]["reasoning_efforts_seen"].append(e)
        if max_input is not None:
            models[m]["max_context_used"] = max(models[m]["max_context_used"], max_input)
        models[m]["threads"] += 1
        context["by_thread"][tid] = {
            "model": m, "max_input_tokens": max_input,
            "mean_input_tokens": round(mean_input, 1) if mean_input is not None else None,
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
            "opencodex_version": _run([shutil.which("opencodex") or "opencodex", "--version"]),
            "opencodex_submodule_pin": None,
            "config_facts": {},
            "codex_proxy_fallback_config": None,
        }
        sub = Path(__file__).resolve().parents[1].parent / "opencodex"
        opencodex["opencodex_submodule_pin"] = _run(
            ["git", "-C", str(sub), "describe", "--tags"]) if sub.exists() else None
        cfg = Path(args.ocx_config)
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
        fallback = cd.codex_home() / "opencodex.config.toml"
        if fallback.exists():
            opencodex["codex_proxy_fallback_config"] = str(fallback)

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
        "subagents": subagents,
        "opencodex": opencodex,
        "prompts": prompts,
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
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"wrote {out_path}")
    print(f"harness={harness['harness']} cli={harness['cli_version']} "
          f"threads={len(all_ids)} subagents={len(subagents)} "
          f"non_vanilla={non_vanilla or 'no'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Capture the bundled, redacted config stack that governed a contestant run.

Credentials-bearing files (auth.json, codex-accounts.json, ...) are recorded
as presence + sha256 only — their content is never embedded.

Usage:
  python3 evaluation/tools/extract_configs.py --workspace <contestant-workspace>
    [--out PATH] [--codex-home PATH] [--opencode-config-dir PATH]
    [--opencodex-config-dir PATH] [--max-bytes N]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from datetime import datetime, timezone
from pathlib import Path

from cfdeval import codex_data as cd
from cfdeval import redact


DIGEST_ONLY = {
    "auth.json": "credentials file — presence and digest only, content never captured",
    "codex-accounts.json": "credentials file — presence and digest only, content never captured",
    "admin-api-token": "credentials file — presence and digest only",
    "installation_id": "device identifier — presence and digest only",
}

DEFAULT_CAPS = {
    "default": 128 * 1024,
    "opencodex-catalog.json": 512 * 1024,
    "models_cache.json": 512 * 1024,
    "catalog-backup.json": 512 * 1024,
    "catalog-backup-*.json": 512 * 1024,
}


def _cap_for(name: str, caps: dict) -> int:
    if name in caps:
        return caps[name]
    for pat, cap in caps.items():
        if re.fullmatch(pat.replace(".", r"\.").replace("*", ".*"), name):
            return cap
    return caps["default"]


def capture_file(path: Path, role: str, caps: dict) -> dict:
    entry = {
        "role": role,
        "kind": "file",
        "path": str(path),
        "exists": False,
        "sha256": None,
        "bytes": None,
        "content": None,
        "content_included": False,
        "redacted": False,
        "redaction_hits": 0,
        "truncated": False,
        "notes": [],
    }
    if not path.is_file():
        return entry
    data = path.read_bytes()
    entry["exists"] = True
    entry["sha256"] = hashlib.sha256(data).hexdigest()
    entry["bytes"] = len(data)
    if path.name in DIGEST_ONLY:
        entry["notes"].append(DIGEST_ONLY[path.name])
        return entry
    text = data.decode("utf-8", errors="replace")
    doc = redact.redact_document(text)
    content, truncated = redact.truncate(doc["content"], _cap_for(path.name, caps))
    entry.update({
        "content": content,
        "content_included": True,
        "redacted": doc["redacted"],
        "redaction_hits": doc["redaction_hits"],
        "truncated": truncated,
    })
    return entry


def capture_dir_files(directory: Path, role: str, caps: dict,
                      suffixes=(".json", ".jsonc", ".toml", ".md", ".sh"),
                      max_files: int = 60, max_depth: int = 3) -> list[dict]:
    """Capture small config files under a directory (bounded)."""
    out = []
    if not directory.is_dir():
        return out
    for p in sorted(directory.rglob("*")):
        if not p.is_file() or p.name in DIGEST_ONLY:
            continue
        rel = p.relative_to(directory)
        if len(rel.parts) > max_depth or p.suffix not in suffixes:
            continue
        if p.stat().st_size > 512 * 1024:
            continue
        out.append(capture_file(p, role, caps))
        if len(out) >= max_files:
            break
    return out


def plugin_manifests(plugins_root: Path, caps: dict) -> list[dict]:
    out = []
    if not plugins_root.is_dir():
        return out
    for source in sorted(p for p in plugins_root.iterdir() if p.is_dir()):
        for name_dir in sorted(p for p in source.iterdir() if p.is_dir()):
            for ver in sorted(p for p in name_dir.iterdir() if p.is_dir()):
                for cand in (ver / ".codex-plugin" / "plugin.json", ver / ".app.json"):
                    if cand.is_file():
                        entry = capture_file(cand, "plugin_manifest", caps)
                        entry["plugin"] = {
                            "source": source.name,
                            "name": name_dir.name,
                            "version": ver.name,
                        }
                        out.append(entry)
    return out


def capture(workspace: Path, codex_home: Path, opencode_config_dir: Path,
            opencodex_config_dir: Path, caps: dict | None = None) -> dict:
    caps = caps or DEFAULT_CAPS
    ws = workspace.resolve()
    entries: list[dict] = []

    # codex user-level config
    ch = codex_home
    for name in ("config.toml", "ocx.config.toml", "opencodex.config.toml",
                 "AGENTS.md", "codex-runtime.json", "version.json",
                 "opencodex-catalog.json", "models_cache.json"):
        entries.append(capture_file(ch / name, "codex", caps))
    rules = capture_dir_files(ch / "rules", "codex_rules", caps)
    entries.extend(rules)
    entries.extend(plugin_manifests(ch / "plugins", caps))
    for name in ("auth.json", "installation_id"):
        entries.append(capture_file(ch / name, "codex", caps))

    # opencode user-level config
    oc = opencode_config_dir
    for name in ("opencode.jsonc", "tui.json", "agents.json"):
        entries.append(capture_file(oc / name, "opencode", caps))
    entries.extend(capture_dir_files(oc / "agent", "opencode_agent", caps))
    entries.extend(capture_dir_files(oc / "command", "opencode_command", caps))
    entries.extend(capture_dir_files(oc / "rules", "opencode_rules", caps))
    oc_data = ws / ".sessions" / "opencode-data" / "opencode"
    entries.append(capture_file(oc_data / "auth.json", "opencode", caps))

    # opencodex user-level config
    ox = opencodex_config_dir
    for name in ("config.json", "version.json", "codex-runtime.json",
                 "responses-state.json", "codex-quota-cache.json"):
        entries.append(capture_file(ox / name, "opencodex", caps))
    for name in ("codex-accounts.json", "admin-api-token"):
        entries.append(capture_file(ox / name, "opencodex", caps))
    for cand in sorted(ox.glob("catalog-backup*.json")):
        entries.append(capture_file(cand, "opencodex", caps))

    # workspace-local configs
    entries.append(capture_file(ws / "AGENTS.md", "workspace", caps))
    entries.append(capture_file(ws / ".codex" / "config.toml", "workspace", caps))
    entries.append(capture_file(ws / ".codex" / "AGENTS.md", "workspace", caps))
    entries.append(capture_file(ws / ".opencode" / "opencode.jsonc", "workspace", caps))
    entries.append(capture_file(ws / ".opencodex" / "config.json", "workspace", caps))
    entries.append(capture_file(ws / ".gitmodules", "workspace", caps))
    ext = ws / "external"
    entries.append({
        "role": "workspace",
        "kind": "symlink" if ext.is_symlink() else "file",
        "path": str(ext),
        "exists": ext.exists() or ext.is_symlink(),
        "sha256": None,
        "bytes": None,
        "content": str(os.readlink(ext)) if ext.is_symlink() else None,
        "content_included": ext.is_symlink(),
        "redacted": False,
        "redaction_hits": 0,
        "truncated": False,
        "notes": ["external dependency symlink target"],
    })

    # benchmark task + rubric pinned by the submodule
    bm = ws / "cfd_solver_agentic_benchmark"
    if bm.is_dir():
        for rel in ("TASK.md", "examiner/SCORING_RUBRIC.md",
                    "examiner/README_EXAMINER.md"):
            entries.append(capture_file(bm / rel, "benchmark", caps))

    redacted_total = sum(e["redaction_hits"] for e in entries)
    return {
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "capture_host": os.uname().nodename,
        "sources": {
            "workspace": str(ws),
            "codex_home": str(ch),
            "opencode_config_dir": str(oc),
            "opencodex_config_dir": str(ox),
        },
        "redaction": {
            "policy": "secret-like values and credential files are never embedded; "
                      "see cfdeval/redact.py",
            "total_hits": redacted_total,
            "marker": redact.MARK,
        },
        "configs": entries,
        "notes": [
            "content cap per file: " + ", ".join(
                f"{k}={v}" for k, v in caps.items()),
            "auth.json / codex-accounts.json / admin-api-token are recorded as "
            "presence + sha256 only (never content)",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Capture verbose redacted configs for a run")
    ap.add_argument("--workspace", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--codex-home", default=None)
    ap.add_argument("--opencode-config-dir", default=None)
    ap.add_argument("--opencodex-config-dir", default=None)
    ap.add_argument("--max-bytes", type=int, default=None,
                    help="override the default per-file content cap (bytes)")
    args = ap.parse_args(argv)

    ws = Path(args.workspace).resolve()
    paths = cd.local_telemetry_paths(
        ws, codex_root=args.codex_home,
        opencode_config_dir=args.opencode_config_dir,
        opencodex_config_dir=args.opencodex_config_dir)
    eval_root = Path(__file__).resolve().parents[2]
    out_path = Path(args.out) if args.out else (
        eval_root / "outputs" / ws.name / "configs.json")
    caps = dict(DEFAULT_CAPS)
    if args.max_bytes:
        caps["default"] = args.max_bytes
    doc = capture(ws, paths["codex_root"], paths["opencode_config_dir"],
                  paths["opencodex_config_dir"], caps)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(doc, indent=2) + "\n")
    n = len(doc["configs"])
    inc = sum(1 for e in doc["configs"] if e["content_included"])
    red = sum(1 for e in doc["configs"] if e["redacted"])
    print(f"wrote {out_path}")
    print(f"configs={n} content_included={inc} redacted={red} "
          f"redaction_hits={doc['redaction']['total_hits']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

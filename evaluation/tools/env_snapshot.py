#!/usr/bin/env python3
"""Environment snapshot for a benchmark run.

Normally run this BEFORE starting the agent inside the real environment (host
or container) so the evaluation can later prove what env the run saw:
host/container facts, tool versions, proxy/network env (redacted), and the
workspace git state.

Usage:
  python3 evaluation/tools/env_snapshot.py --workspace <contestant-workspace>
    [--out <path>] [--probe-proxy]

For an operator-approved legacy run that was not snapshotted before execution:
  python3 evaluation/tools/env_snapshot.py --workspace <contestant-workspace> \
    --capture-phase post_run --initial-branch <harness>/<model>/init \
    --initial-commit <full-sha> --reconstruction-source <description>

Default output: <workspace>/.eval/env_snapshot.json (git-excluded; vendored
into docker/scripts/setup-workspace.sh). The evaluation pipeline copies it
into the result snapshot as env_snapshot.json when present; older runs
without it are recorded as `captured: false`.

Stdlib only — runs on a bare host without the cfdeval package.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import socket
import subprocess
import sys
import urllib.parse
from datetime import datetime, timezone
from pathlib import Path


VERSION = "1.1"

REDACT_MARK = "***REDACTED***"
_SK_KEY = re.compile(r"\bsk-[A-Za-z0-9_\-]{8,}")
_BEARER = re.compile(r"(?i)\bBearer\s+\S+")
_ASSIGN = re.compile(
    r"(?i)(api[_-]?key|access[_-]?token|refresh[_-]?token|secret|password|"
    r"passwd|token)\b\s*[:=]\s*[\"']?[A-Za-z0-9._~+/=-]{8,}",
)
_PROXY_CREDS = re.compile(r"(?i)(https?://)([^/\s:@]+):([^@/\s]+)@")

ENV_VARS = [
    "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY",
    "http_proxy", "https_proxy", "all_proxy", "no_proxy",
    "CODEX_HOME", "OPENCODE_CONFIG", "OPENCODE_DATA", "OPENCODE_DISABLE_HUSKY",
    "OCX_PORT", "OCX_HOST", "OCX_HOME", "OPENCODEX_HOME",
    "SHELL", "USER", "LOGNAME", "HOME", "TZ", "LANG", "LC_ALL",
]

CREDENTIAL_ENV_KEYS = (
    "OPENAI_API_KEY", "ANTHROPIC_API_KEY", "DEEPSEEK_API_KEY", "BLSC_API_KEY",
    "GITHUB_TOKEN", "HF_TOKEN", "AZURE_OPENAI_API_KEY", "AWS_ACCESS_KEY_ID",
    "AWS_SECRET_ACCESS_KEY", "GOOGLE_API_KEY", "OPENCODE_API_KEY",
    "OPENCODEX_API_KEY",
)

TOOL_COMMANDS = {
    "codex": ["codex", "--version"],
    "opencode": ["opencode", "--version"],
    "opencodex": ["opencodex", "--version"],
    "uv": ["uv", "--version"],
    "python3": ["python3", "--version"],
    "git": ["git", "--version"],
    "cmake": ["cmake", "--version"],
    "mpirun": ["mpirun", "--version"],
    "mpicc": ["mpicc", "--version"],
    "gcc": ["gcc", "--version"],
    "docker": ["docker", "--version"],
    "codegraph": ["codegraph", "--version"],
}


def _run(cmd: list[str], timeout: int = 15) -> str | None:
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout.strip().splitlines()[0] if r.stdout.strip() else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def _git(args: list[str], cwd: Path) -> str | None:
    try:
        r = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True,
                           text=True, timeout=30)
        return r.stdout.strip() if r.returncode == 0 else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def redact_value(value: str, cap: int = 40) -> str:
    v = value
    v = _SK_KEY.sub("sk-" + REDACT_MARK, v)
    v = _BEARER.sub("Bearer " + REDACT_MARK, v)
    v = _ASSIGN.sub(lambda m: m.group(1) + "=" + REDACT_MARK, v)
    v = _PROXY_CREDS.sub(lambda m: m.group(1) + m.group(2) + ":" + REDACT_MARK + "@", v)
    if len(v) > cap:
        v = v[: cap // 2] + "..." + v[-cap // 2:]
    return v


def env_section() -> dict:
    envs = []
    for name in ENV_VARS:
        val = os.environ.get(name)
        if val is None:
            continue
        envs.append({"name": name, "set": True, "value": redact_value(val)})
    for name in sorted(os.environ):
        if name in CREDENTIAL_ENV_KEYS or (name.upper() in CREDENTIAL_ENV_KEYS):
            val = os.environ.get(name, "")
            envs.append({
                "name": name,
                "set": bool(val),
                "value": redact_value(val),
                "credential": True,
            })
    return {"count": len(envs), "vars": envs}


def probe_proxy(proxy_url: str) -> dict:
    """TCP-connect probe of a proxy URL (no data sent)."""
    try:
        u = urllib.parse.urlparse(proxy_url)
        host, port = u.hostname, u.port
        if not host or not port:
            return {"url": redact_value(proxy_url), "reachable": None,
                    "note": "no port in proxy URL"}
        s = socket.create_connection((host, port), timeout=3)
        s.close()
        return {"url": redact_value(proxy_url), "reachable": True, "host": host,
                "port": port}
    except OSError as exc:
        return {"url": redact_value(proxy_url), "reachable": False,
                "error": str(exc)[:120]}


def host_section() -> dict:
    mem_gb = None
    try:
        with open("/proc/meminfo") as fh:
            for line in fh:
                if line.startswith("MemTotal:"):
                    mem_gb = round(int(line.split()[1]) / 1024 / 1024, 1)
                    break
    except OSError:
        pass
    loadavg = None
    try:
        with open("/proc/loadavg") as fh:
            loadavg = fh.read().strip().split()[:3]
    except OSError:
        pass
    in_docker = Path("/.dockerenv").exists()
    docker = {}
    if in_docker:
        docker["container_id"] = os.uname().nodename
        # best-effort image id from cgroup v2 scope name
        try:
            scope = Path("/proc/self/cgroup").read_text().strip().splitlines()[-1]
            docker["cgroup"] = scope[:200]
        except OSError:
            docker["cgroup"] = None
    else:
        docker["container_id"] = None
    return {
        "hostname": os.uname().nodename,
        "os": f"{platform.system()} {platform.release()}",
        "machine": platform.machine(),
        "python": platform.python_version(),
        "cpus": os.cpu_count(),
        "memory_gb": mem_gb,
        "loadavg_1_5_15": loadavg,
        "in_docker": in_docker,
        "docker": docker,
    }


def tools_section() -> dict:
    tools = {}
    for name, cmd in TOOL_COMMANDS.items():
        path = shutil.which(cmd[0])
        tools[name] = {
            "installed": path is not None,
            "path": path,
            "version": _run(cmd) if path else None,
        }
    return tools


def workspace_section(ws: Path) -> dict:
    agents_md = ws / "AGENTS.md"
    sha = None
    if agents_md.is_file():
        sha = hashlib.sha256(agents_md.read_bytes()).hexdigest()
    bm = ws / "cfd_solver_agentic_benchmark"
    dirty = _git(["status", "--porcelain"], ws)
    return {
        "path": str(ws),
        "branch": _git(["rev-parse", "--abbrev-ref", "HEAD"], ws),
        "commit": _git(["rev-parse", "HEAD"], ws),
        "git_dirty": bool(dirty),
        "dirty_files": len(dirty.splitlines()) if dirty else 0,
        "agents_md_sha256": sha,
        "codegraph": (ws / ".codegraph").is_dir(),
        "done_marker": (ws / "done").exists(),
        "external_symlink": str(os.readlink(ws / "external")) if (ws / "external").is_symlink() else None,
        "benchmark_submodule": {
            "exists": bm.is_dir(),
            "commit": _git(["rev-parse", "HEAD"], bm) if bm.is_dir() else None,
            "branch": _git(["rev-parse", "--abbrev-ref", "HEAD"], bm) if bm.is_dir() else None,
        },
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Capture a benchmark environment snapshot")
    ap.add_argument("--workspace", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--probe-proxy", action="store_true",
                    help="TCP-probe configured proxy endpoints")
    ap.add_argument("--capture-phase", choices=("pre_run", "post_run"),
                    default="pre_run")
    ap.add_argument("--initial-branch",
                    help="operator-approved reconstructed initial branch (post_run only)")
    ap.add_argument("--initial-commit",
                    help="operator-approved reconstructed full initial commit (post_run only)")
    ap.add_argument("--reconstruction-source", action="append", default=[],
                    help="evidence used to reconstruct legacy provenance; repeatable")
    args = ap.parse_args(argv)

    ws = Path(args.workspace).resolve()
    out_path = Path(args.out) if args.out else ws / ".eval" / "env_snapshot.json"

    if args.capture_phase == "pre_run" and (
        args.initial_branch or args.initial_commit or args.reconstruction_source
    ):
        ap.error("reconstruction options require --capture-phase post_run")
    if args.capture_phase == "post_run" and not (
        args.initial_branch and args.initial_commit and args.reconstruction_source
    ):
        ap.error("post_run requires --initial-branch, --initial-commit, and --reconstruction-source")

    env = env_section()
    proxies = []
    if args.probe_proxy:
        for name in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                     "http_proxy", "https_proxy", "all_proxy"):
            if os.environ.get(name):
                proxies.append({**probe_proxy(os.environ[name]), "env": name})

    captured_workspace = workspace_section(ws)
    authoritative_workspace = dict(captured_workspace)
    limitations = []
    if args.capture_phase == "post_run":
        authoritative_workspace["branch"] = args.initial_branch
        authoritative_workspace["commit"] = args.initial_commit
        authoritative_workspace["state_timing"] = "initial_reconstructed_post_run"
        limitations = [
            "No pre-run environment snapshot exists for this legacy run.",
            "Host, tool, environment, and network values below describe post-run capture time, not the contestant execution environment.",
            "Original harness version and container image identity are unavailable.",
        ]
    else:
        authoritative_workspace["state_timing"] = "initial_observed_pre_run"

    doc = {
        "schema": "env_snapshot",
        "version": VERSION,
        "capture_phase": args.capture_phase,
        "run_environment_available": args.capture_phase == "pre_run",
        "runtime_at_run": {
            "availability": "captured" if args.capture_phase == "pre_run" else "unavailable",
            "harness_version": None if args.capture_phase == "post_run" else "see tools and provenance",
            "container_image": None if args.capture_phase == "post_run" else captured_workspace.get("container_image"),
        },
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "local_time": datetime.now().astimezone().isoformat(timespec="seconds"),
        "host": host_section(),
        "tools": tools_section(),
        "environment": env,
        "network": {"proxy_env": proxies, "note": "only local TCP probes, no external requests"},
        "workspace": authoritative_workspace,
        "provenance": {
            "script": str(Path(__file__).resolve()),
            "script_version": VERSION,
            "reconstruction_sources": args.reconstruction_source,
            "captured_workspace_state": captured_workspace if args.capture_phase == "post_run" else None,
            "limitations": limitations,
        },
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(doc, indent=2) + "\n")
    print(f"wrote {out_path}")
    print(f"host={doc['host']['hostname']} in_docker={doc['host']['in_docker']} "
          f"cpus={doc['host']['cpus']} mem_gb={doc['host']['memory_gb']}")
    print(f"workspace={ws.name} branch={doc['workspace']['branch']} "
          f"commit={doc['workspace']['commit']} dirty={doc['workspace']['git_dirty']}")
    print(f"tools: " + ", ".join(
        f"{k}={v['version'] or 'missing'}" for k, v in doc["tools"].items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

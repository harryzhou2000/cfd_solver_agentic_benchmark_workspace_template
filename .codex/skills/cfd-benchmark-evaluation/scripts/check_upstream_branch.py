#!/usr/bin/env python3
"""Check that an operator-numbered result branch is absent upstream and locally."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


NUMBER_RE = re.compile(r"^[0-9]+$")
BRANCH_PART_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


def run(args: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"could not execute {' '.join(args)}: {exc}") from exc


def result_branch(snapshot_path: Path, number: str) -> tuple[str, str]:
    if not NUMBER_RE.fullmatch(number):
        raise SystemExit("number must contain digits only; leading zeros are preserved")
    if not snapshot_path.is_file():
        raise SystemExit(f"missing pre-run environment snapshot: {snapshot_path}")
    snapshot = json.loads(snapshot_path.read_text())
    initial_branch = (snapshot.get("workspace") or {}).get("branch")
    if not isinstance(initial_branch, str):
        raise SystemExit("environment snapshot lacks workspace.branch")
    parts = initial_branch.split("/")
    if (
        len(parts) != 3
        or parts[-1] != "init"
        or any(not BRANCH_PART_RE.fullmatch(part) for part in parts[:-1])
    ):
        raise SystemExit(
            f"initial branch must have the form <harness>/<model>/init, got {initial_branch!r}"
        )
    return initial_branch, "/".join([*parts[:-1], number])


def manager_upstream() -> str:
    manager_root = Path(__file__).resolve().parents[4]
    result = run(["git", "remote", "get-url", "origin"], manager_root)
    if result.returncode != 0 or not result.stdout.strip():
        raise SystemExit(
            "manager repository origin is unavailable; pass an operator-approved --upstream URL"
        )
    return result.stdout.strip()


def workspace_origin_status(workspace: Path, canonical_upstream: str) -> str:
    """Inspect contestant origin without mutating intentionally omitted remotes."""
    current = run(["git", "remote", "get-url", "origin"], workspace)
    if current.returncode != 0:
        remotes = run(["git", "remote"], workspace)
        if remotes.returncode != 0:
            raise SystemExit(f"workspace remote inventory failed: {remotes.stderr.strip()}")
        if "origin" in remotes.stdout.splitlines():
            raise SystemExit(
                "workspace origin exists but its URL is unreadable; repair it manually"
            )
        return "absent_expected"
    workspace_upstream = current.stdout.strip()
    if workspace_upstream != canonical_upstream:
        raise SystemExit(
            "workspace origin does not match the canonical manager upstream: "
            f"{workspace_upstream!r} != {canonical_upstream!r}; do not overwrite it silently"
        )
    return "matching"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--number", required=True)
    parser.add_argument(
        "--upstream",
        help="operator-approved canonical upstream URL; default: manager repository origin URL",
    )
    parser.add_argument("--env-snapshot", help="default: <workspace>/.eval/env_snapshot.json")
    args = parser.parse_args()

    workspace = Path(args.workspace).resolve()
    snapshot_path = (
        Path(args.env_snapshot).resolve()
        if args.env_snapshot
        else workspace / ".eval" / "env_snapshot.json"
    )
    initial_branch, branch = result_branch(snapshot_path, args.number)
    upstream = args.upstream or manager_upstream()
    origin_status = workspace_origin_status(workspace, upstream)
    ref = f"refs/heads/{branch}"

    local = run(["git", "show-ref", "--verify", "--quiet", ref], workspace)
    if local.returncode not in (0, 1):
        raise SystemExit(f"local branch check failed: {local.stderr.strip()}")

    remote = run(["git", "ls-remote", "--exit-code", "--heads", upstream, ref], workspace)
    if remote.returncode not in (0, 2):
        raise SystemExit(
            "upstream collision check failed; availability is unknown: "
            + (remote.stderr.strip() or f"git ls-remote exited {remote.returncode}")
        )
    upstream_exists = remote.returncode == 0 and bool(remote.stdout.strip())
    result = {
        "initial_branch": initial_branch,
        "operator_number": args.number,
        "result_branch": branch,
        "ref": ref,
        "upstream": upstream,
        "workspace_origin_status": origin_status,
        "workspace_origin_mutated": False,
        # Retain the legacy field for consumers while making the no-mutation
        # behavior explicit.
        "workspace_origin_added": False,
        "local_exists": local.returncode == 0,
        "upstream_exists": upstream_exists,
        "available": local.returncode == 1 and not upstream_exists,
    }
    print(json.dumps(result, indent=2))
    if not result["available"]:
        where = []
        if result["local_exists"]:
            where.append("locally")
        if result["upstream_exists"]:
            where.append("upstream")
        raise SystemExit(f"result branch collision {' and '.join(where)}: {branch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

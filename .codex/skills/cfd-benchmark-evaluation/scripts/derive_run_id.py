#!/usr/bin/env python3
"""Derive a canonical CFD benchmark run ID from pre-run provenance and artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


NUMBER_RE = re.compile(r"^[0-9]+$")
BRANCH_PART_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")

DEFAULT_ARTIFACT_GROUPS = (
    ("solver/CMakeLists.txt", "CMakeLists.txt"),
    (
        "report/report.tex",
        "cfd_solver_agentic_benchmark/report/report.tex",
        "solver/report/report.tex",
        "cfd_solver_agentic_benchmark/solver/report/report.tex",
    ),
    (
        "report/run_manifest.csv",
        "report/run_manifest.md",
        "cfd_solver_agentic_benchmark/report/run_manifest.csv",
        "cfd_solver_agentic_benchmark/report/run_manifest.md",
        "solver/report/run_manifest.csv",
        "solver/report/run_manifest.md",
    ),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_git_tracked(workspace: Path, relpaths: list[str]) -> None:
    try:
        result = subprocess.run(
            ["git", "-C", str(workspace), "ls-files", "--error-unmatch", "--", *relpaths],
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"could not verify artifact tracking: {exc}") from exc
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SystemExit(
            "all run-ID artifacts must be Git tracked"
            + (f": {detail}" if detail else "")
        )


def contained_file(workspace: Path, rel: str) -> tuple[str, Path]:
    candidate = Path(rel)
    if candidate.is_absolute():
        raise SystemExit(f"artifact must be workspace-relative: {rel}")
    normalized = candidate.as_posix()
    if normalized in ("", ".") or normalized.startswith("../"):
        raise SystemExit(f"artifact escapes workspace: {rel}")
    resolved = (workspace / candidate).resolve()
    try:
        resolved.relative_to(workspace)
    except ValueError as exc:
        raise SystemExit(f"artifact escapes workspace through symlink: {rel}") from exc
    if not resolved.is_file():
        raise SystemExit(f"artifact is not a regular file: {rel}")
    return normalized, resolved


def select_artifacts(workspace: Path, explicit: list[str]) -> list[tuple[str, Path]]:
    if explicit:
        if not 1 <= len(explicit) <= 3:
            raise SystemExit("select exactly 1 to 3 --artifact files")
        selected = [contained_file(workspace, rel) for rel in explicit]
    else:
        selected = []
        for group in DEFAULT_ARTIFACT_GROUPS:
            for rel in group:
                path = workspace / rel
                if path.is_file() and not path.is_symlink():
                    selected.append(contained_file(workspace, rel))
                    break
        if not selected:
            raise SystemExit(
                "no stable default submission artifact found; pass 1 to 3 --artifact RELPATH options"
            )
    paths = [rel for rel, _ in selected]
    if len(paths) != len(set(paths)):
        raise SystemExit("artifact paths must be unique")
    forbidden_roots = {".eval", ".sessions", ".git", "build", "external"}
    for rel in paths:
        parts = Path(rel).parts
        if rel == "done" or (parts and parts[0] in forbidden_roots):
            raise SystemExit(f"unstable or non-submission artifact is forbidden: {rel}")
    require_git_tracked(workspace, paths)
    return sorted(selected)


def parse_initial_branch(branch: str, number: str) -> tuple[str, str]:
    parts = branch.split("/")
    if len(parts) != 3 or parts[-1] != "init":
        raise SystemExit(
            f"initial branch must have the form <harness>/<model>/init, got {branch!r}"
        )
    identity = parts[:-1]
    if any(not BRANCH_PART_RE.fullmatch(part) for part in identity):
        raise SystemExit(f"initial branch contains an unsupported component: {branch!r}")
    result_branch = "/".join([*identity, number])
    run_id_base = "_".join([*identity, number])
    return result_branch, run_id_base


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--number", required=True)
    parser.add_argument("--artifact", action="append", default=[])
    parser.add_argument("--out", help="also write the JSON identity record here")
    parser.add_argument(
        "--env-snapshot",
        help="default: <workspace>/.eval/env_snapshot.json",
    )
    args = parser.parse_args()

    if not NUMBER_RE.fullmatch(args.number):
        raise SystemExit(
            "number must contain digits only; leading zeros are preserved"
        )
    workspace = Path(args.workspace).resolve()
    snapshot_path = (
        Path(args.env_snapshot).resolve()
        if args.env_snapshot
        else workspace / ".eval" / "env_snapshot.json"
    )
    if not snapshot_path.is_file():
        raise SystemExit(f"missing pre-run environment snapshot: {snapshot_path}")
    snapshot = json.loads(snapshot_path.read_text())
    provenance = snapshot.get("workspace") or {}
    initial_branch = provenance.get("branch")
    initial_commit = provenance.get("commit")
    if not isinstance(initial_branch, str) or not initial_branch:
        raise SystemExit("environment snapshot lacks workspace.branch")
    if not isinstance(initial_commit, str) or not re.fullmatch(r"[0-9a-fA-F]{40,64}", initial_commit):
        raise SystemExit("environment snapshot lacks a full workspace.commit hash")

    result_branch, run_id_base = parse_initial_branch(initial_branch, args.number)
    artifacts = []
    for rel, path in select_artifacts(workspace, args.artifact):
        artifacts.append({"path": rel, "sha256": sha256_file(path)})

    canonical_lines = [f"initial_commit\t{initial_commit.lower()}"]
    canonical_lines.extend(
        f"artifact\t{item['path']}\t{item['sha256']}" for item in artifacts
    )
    canonical_record = "\n".join(canonical_lines) + "\n"
    full_state_hash = hashlib.sha256(canonical_record.encode("utf-8")).hexdigest()
    run_id = f"{run_id_base}_{full_state_hash[:6]}"
    result = {
        "run_id": run_id,
        "run_id_base": run_id_base,
        "operator_number": args.number,
        "initial_branch": initial_branch,
        "initial_commit": initial_commit.lower(),
        "result_branch": result_branch,
        "state_hash": full_state_hash[:6],
        "state_hash_sha256": full_state_hash,
        "artifacts": artifacts,
        "canonical_record": canonical_record,
        "env_snapshot": str(snapshot_path),
        "workspace": str(workspace),
    }
    rendered = json.dumps(result, indent=2) + "\n"
    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(rendered)
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

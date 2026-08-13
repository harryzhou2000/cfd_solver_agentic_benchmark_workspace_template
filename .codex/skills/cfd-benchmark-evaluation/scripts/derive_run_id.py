#!/usr/bin/env python3
"""Derive a CFD benchmark run ID only from immutable Git commit state."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path, PurePosixPath


NUMBER_RE = re.compile(r"^[0-9]+$")
BRANCH_PART_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
OBJECT_ID_RE = re.compile(r"^[0-9a-f]+$")

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


def git(workspace: Path, args: list[str], *, binary: bool = False) -> bytes | str:
    try:
        result = subprocess.run(
            ["git", "-C", str(workspace), *args],
            capture_output=True,
            text=not binary,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise SystemExit(f"git command failed: {exc}") from exc
    if result.returncode != 0:
        stderr = result.stderr.decode(errors="replace") if binary else result.stderr
        raise SystemExit(f"git {' '.join(args)} failed: {stderr.strip()}")
    return result.stdout


def resolve_commit(workspace: Path, revision: str, label: str) -> str:
    resolved = str(git(workspace, ["rev-parse", "--verify", f"{revision}^{{commit}}"])).strip()
    if not OBJECT_ID_RE.fullmatch(resolved):
        raise SystemExit(f"{label} did not resolve to a full Git commit object ID")
    return resolved


def normalize_artifact(rel: str) -> str:
    if "\\" in rel:
        raise SystemExit(f"artifact path must use repository '/' separators: {rel}")
    if any(char in rel for char in ("\0", "\n", "\r", "\t")):
        raise SystemExit(f"artifact path contains unsupported control characters: {rel!r}")
    path = PurePosixPath(rel)
    if path.is_absolute() or rel in ("", ".") or any(part in ("", ".", "..") for part in path.parts):
        raise SystemExit(f"artifact must be a normalized repository-relative path: {rel}")
    normalized = path.as_posix()
    forbidden_roots = {".eval", ".sessions", ".git", "build", "external"}
    if normalized == "done" or path.parts[0] in forbidden_roots:
        raise SystemExit(f"unstable or non-submission artifact is forbidden: {rel}")
    return normalized


def tree_entry(workspace: Path, commit: str, rel: str) -> tuple[str, str, str] | None:
    raw = str(git(workspace, ["ls-tree", commit, "--", rel])).rstrip("\n")
    if not raw:
        return None
    lines = raw.splitlines()
    if len(lines) != 1:
        raise SystemExit(f"artifact path is ambiguous in commit {commit}: {rel}")
    meta, found_path = lines[0].split("\t", 1)
    mode, obj_type, object_id = meta.split()
    if found_path != rel:
        raise SystemExit(f"artifact path did not resolve exactly in commit: {rel}")
    return mode, obj_type, object_id


def select_artifacts(workspace: Path, commit: str, explicit: list[str]) -> list[str]:
    if explicit:
        if not 1 <= len(explicit) <= 3:
            raise SystemExit("select exactly 1 to 3 --artifact files")
        selected = [normalize_artifact(rel) for rel in explicit]
    else:
        selected = []
        for group in DEFAULT_ARTIFACT_GROUPS:
            for candidate in group:
                rel = normalize_artifact(candidate)
                if tree_entry(workspace, commit, rel) is not None:
                    selected.append(rel)
                    break
        if not selected:
            raise SystemExit(
                "no stable default artifact blob found in the submission commit; "
                "pass 1 to 3 --artifact RELPATH options"
            )
    if len(selected) != len(set(selected)):
        raise SystemExit("artifact paths must be unique")
    for rel in selected:
        entry = tree_entry(workspace, commit, rel)
        if entry is None:
            raise SystemExit(f"artifact is absent from submission commit {commit}: {rel}")
        mode, obj_type, _object_id = entry
        if obj_type != "blob" or mode not in {"100644", "100755"}:
            raise SystemExit(
                f"artifact must be a regular file blob in the submission commit: {rel} "
                f"(mode={mode}, type={obj_type})"
            )
    return sorted(selected)


def hash_commit_blob(workspace: Path, commit: str, rel: str) -> tuple[str, str]:
    entry = tree_entry(workspace, commit, rel)
    assert entry is not None
    _mode, _obj_type, blob_object_id = entry
    content = git(workspace, ["cat-file", "blob", blob_object_id], binary=True)
    assert isinstance(content, bytes)
    return hashlib.sha256(content).hexdigest(), blob_object_id


def parse_initial_branch(branch: str, number: str) -> tuple[str, str]:
    parts = branch.split("/")
    if len(parts) != 3 or parts[-1] != "init":
        raise SystemExit(
            f"initial branch must have the form <harness>/<model>/init, got {branch!r}"
        )
    identity = parts[:-1]
    if any(not BRANCH_PART_RE.fullmatch(part) for part in identity):
        raise SystemExit(f"initial branch contains an unsupported component: {branch!r}")
    return "/".join([*identity, number]), "_".join([*identity, number])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--number", required=True)
    parser.add_argument(
        "--submission-commit",
        required=True,
        help="immutable result commit whose artifact blobs define the state hash",
    )
    parser.add_argument("--artifact", action="append", default=[])
    parser.add_argument("--out", help="also write the JSON identity record here")
    parser.add_argument("--env-snapshot", help="default: <workspace>/.eval/env_snapshot.json")
    args = parser.parse_args()

    if not NUMBER_RE.fullmatch(args.number):
        raise SystemExit("number must contain digits only; leading zeros are preserved")
    workspace = Path(args.workspace).resolve()
    git(workspace, ["rev-parse", "--git-dir"])
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
    initial_revision = provenance.get("commit")
    if not isinstance(initial_branch, str) or not initial_branch:
        raise SystemExit("environment snapshot lacks workspace.branch")
    if not isinstance(initial_revision, str) or not initial_revision:
        raise SystemExit("environment snapshot lacks workspace.commit")

    initial_commit = resolve_commit(workspace, initial_revision, "initial commit")
    submission_commit = resolve_commit(
        workspace, args.submission_commit, "submission commit"
    )
    result_branch, run_id_base = parse_initial_branch(initial_branch, args.number)
    branch_commit = resolve_commit(
        workspace, f"refs/heads/{result_branch}", "result branch tip"
    )
    if branch_commit != submission_commit:
        raise SystemExit(
            f"submission commit {submission_commit} is not the tip of {result_branch} "
            f"({branch_commit})"
        )

    artifacts = []
    for rel in select_artifacts(workspace, submission_commit, args.artifact):
        sha256, blob_object_id = hash_commit_blob(workspace, submission_commit, rel)
        artifacts.append(
            {"path": rel, "sha256": sha256, "git_blob_object_id": blob_object_id}
        )

    canonical_lines = [f"initial_commit\t{initial_commit}"]
    canonical_lines.extend(
        f"artifact\t{item['path']}\t{item['sha256']}" for item in artifacts
    )
    canonical_record = "\n".join(canonical_lines) + "\n"
    full_state_hash = hashlib.sha256(canonical_record.encode("utf-8")).hexdigest()
    run_id = f"{run_id_base}_{full_state_hash[:6]}"
    result = {
        "canonicalization_version": 1,
        "run_id": run_id,
        "run_id_base": run_id_base,
        "operator_number": args.number,
        "initial_branch": initial_branch,
        "initial_commit": initial_commit,
        "result_branch": result_branch,
        "submission_commit": submission_commit,
        "state_hash": full_state_hash[:6],
        "state_hash_sha256": full_state_hash,
        "state_hash_algorithm": "sha256",
        "hash_source": "immutable_git_commit_blobs",
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

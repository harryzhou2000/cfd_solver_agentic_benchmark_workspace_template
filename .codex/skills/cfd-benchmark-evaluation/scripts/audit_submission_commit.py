#!/usr/bin/env python3
"""Reject raw data and generated artifacts changed by a result commit."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path, PurePosixPath


REPORT_FIGURE_PREFIXES = (
    "report/figures/",
    "solver/report/figures/",
    "cfd_solver_agentic_benchmark/report/figures/",
    "cfd_solver_agentic_benchmark/solver/report/figures/",
)
FORBIDDEN_DIR_PARTS = {
    "build", "build-debug", "build-release", ".cache", ".eval", ".sessions",
    "results", "result", "outputs", "output", "visualization", "visualizations",
    "restart", "restarts", "checkpoints", "probe", "probes", "debug_outputs",
}
FORBIDDEN_EXTENSIONS = {
    ".log", ".out", ".stdout", ".stderr", ".bin", ".dat", ".h5", ".hdf5",
    ".cgns", ".vtk", ".vtu", ".pvtu", ".pvd", ".xdmf", ".xmf", ".plt",
    ".tec", ".szplt", ".o", ".obj", ".a", ".so", ".dylib", ".dll", ".exe",
    ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".svg", ".pdf",
    ".csv", ".tsv", ".npy", ".npz", ".parquet", ".feather", ".arrow",
    ".mat", ".pkl", ".pickle", ".sqlite", ".sqlite3", ".db",
}
FORBIDDEN_BASENAMES = {
    "residuals.csv", "forces.csv", "surface.csv", "partition_diagnostics.csv",
    "partition_diagnostics.json", "metadata.json", "run_status.json", "stdout.log",
    "stderr.log", "field_final", "restart_final",
}
RESTART_OR_FIELD = re.compile(r"^(restart|checkpoint|field)(_|\.|-)", re.IGNORECASE)


def git(workspace: Path, args: list[str], *, binary: bool = False) -> bytes | str:
    result = subprocess.run(
        ["git", "-C", str(workspace), *args],
        capture_output=True,
        text=not binary,
        timeout=60,
    )
    if result.returncode != 0:
        stderr = result.stderr.decode(errors="replace") if binary else result.stderr
        raise SystemExit(f"git {' '.join(args)} failed: {stderr.strip()}")
    return result.stdout


def resolve_commit(workspace: Path, revision: str) -> str:
    return str(git(workspace, ["rev-parse", "--verify", f"{revision}^{{commit}}"])).strip()


def classify(path_text: str) -> str | None:
    path = PurePosixPath(path_text)
    lowered_parts = tuple(part.lower() for part in path.parts)
    basename = path.name.lower()
    suffix = path.suffix.lower()
    if any(part in FORBIDDEN_DIR_PARTS or part.startswith("build-") for part in lowered_parts[:-1]):
        return "raw/generated/build directory"
    if basename in FORBIDDEN_BASENAMES or RESTART_OR_FIELD.match(basename):
        return "raw solver result or restart/field artifact"
    if suffix == ".png":
        if not any(path_text.startswith(prefix) for prefix in REPORT_FIGURE_PREFIXES):
            return "PNG outside an approved report figures directory"
        return None
    if suffix in FORBIDDEN_EXTENSIONS:
        return f"prohibited generated/data extension {suffix}"
    return None


def report_tex_path_for_png(path_text: str) -> tuple[str, str] | None:
    for prefix in REPORT_FIGURE_PREFIXES:
        if path_text.startswith(prefix):
            report_root = prefix.removesuffix("figures/")
            relative = path_text[len(report_root):]
            return report_root + "report.tex", relative
    return None


def committed_text(workspace: Path, commit: str, path: str) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(workspace), "show", f"{commit}:{path}"],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode != 0:
        return None
    return result.stdout


def png_is_referenced(workspace: Path, commit: str, path_text: str) -> bool:
    mapping = report_tex_path_for_png(path_text)
    if mapping is None:
        return False
    report_tex, relative = mapping
    text = committed_text(workspace, commit, report_tex)
    if text is None:
        return False
    text = "\n".join(line.split("%", 1)[0] for line in text.splitlines())
    without_extension = str(PurePosixPath(relative).with_suffix(""))
    candidates = {relative, without_extension}
    return any(candidate in text for candidate in candidates)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--submission-commit", required=True)
    parser.add_argument("--env-snapshot", help="default: <workspace>/.eval/env_snapshot.json")
    args = parser.parse_args()
    workspace = Path(args.workspace).resolve()
    snapshot_path = (
        Path(args.env_snapshot).resolve()
        if args.env_snapshot
        else workspace / ".eval" / "env_snapshot.json"
    )
    snapshot = json.loads(snapshot_path.read_text())
    initial_revision = (snapshot.get("workspace") or {}).get("commit")
    if not isinstance(initial_revision, str) or not initial_revision:
        raise SystemExit("environment snapshot lacks workspace.commit")
    initial_commit = resolve_commit(workspace, initial_revision)
    submission_commit = resolve_commit(workspace, args.submission_commit)

    raw = git(
        workspace,
        ["diff", "--name-status", "-z", "--find-renames", initial_commit, submission_commit],
        binary=True,
    )
    assert isinstance(raw, bytes)
    fields = raw.split(b"\0")
    changed: list[dict[str, str]] = []
    violations: list[dict[str, str]] = []
    index = 0
    while index < len(fields) and fields[index]:
        status = fields[index].decode("utf-8", errors="surrogateescape")
        index += 1
        old_path = None
        if status.startswith(("R", "C")):
            old_path = fields[index].decode("utf-8", errors="surrogateescape")
            index += 1
        path = fields[index].decode("utf-8", errors="surrogateescape")
        index += 1
        item = {"status": status, "path": path}
        if old_path is not None:
            item["old_path"] = old_path
        changed.append(item)
        reason = classify(path)
        if reason is None and old_path is not None:
            old_reason = classify(old_path)
            if old_reason is not None:
                reason = "renamed inherited prohibited/data path; requires operator review: " + old_reason
        if (
            reason is None
            and path.lower().endswith(".png")
            and not status.startswith("D")
            and not png_is_referenced(workspace, submission_commit, path)
        ):
            reason = "report PNG is not referenced by committed report.tex"
        if reason is not None:
            if status.startswith("D"):
                reason = "deleted inherited prohibited/data path; requires operator review: " + reason
            violations.append({**item, "reason": reason})

    result = {
        "initial_commit": initial_commit,
        "submission_commit": submission_commit,
        "changed_count": len(changed),
        "changed": changed,
        "violations": violations,
        "passed": not violations,
        "note": "Pattern audit only; evaluator must still inspect every allowed path.",
    }
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

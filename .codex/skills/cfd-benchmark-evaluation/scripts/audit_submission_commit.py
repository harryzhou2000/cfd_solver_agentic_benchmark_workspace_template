#!/usr/bin/env python3
"""Audit the final submission tree while preserving contestant Git history."""

from __future__ import annotations

import argparse
import json
import posixpath
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
    ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".svg",
    ".csv", ".tsv", ".npy", ".npz", ".parquet", ".feather", ".arrow",
    ".mat", ".pkl", ".pickle", ".sqlite", ".sqlite3", ".db",
    ".aux", ".bbl", ".bcf", ".blg", ".fdb_latexmk", ".fls", ".nav",
    ".snm", ".toc", ".vrb",
}
FORBIDDEN_BASENAMES = {
    "residuals.csv", "forces.csv", "surface.csv", "partition_diagnostics.csv",
    "partition_diagnostics.json", "metadata.json", "run_status.json", "stdout.log",
    "stderr.log", "field_final", "restart_final", "sanity_checks.json",
}
RESTART_OR_FIELD = re.compile(r"^(restart|checkpoint|field)(_|\.|-)", re.IGNORECASE)
SOURCE_TREE_MARKERS = {"src", "source", "include"}


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


def is_ancestor(workspace: Path, ancestor: str, descendant: str) -> bool:
    result = subprocess.run(
        ["git", "-C", str(workspace), "merge-base", "--is-ancestor", ancestor, descendant],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode == 0:
        return True
    if result.returncode == 1:
        return False
    raise SystemExit(f"git merge-base --is-ancestor failed: {result.stderr.strip()}")


def changed_paths(workspace: Path, old: str, new: str) -> list[dict[str, str]]:
    raw = git(
        workspace,
        ["diff", "--name-status", "-z", "--find-renames", old, new],
        binary=True,
    )
    assert isinstance(raw, bytes)
    fields = raw.split(b"\0")
    changed: list[dict[str, str]] = []
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
    return changed


def classify(path_text: str) -> str | None:
    path = PurePosixPath(path_text)
    lowered_parts = tuple(part.lower() for part in path.parts)
    basename = path.name.lower()
    suffix = path.suffix.lower()
    directory_parts = lowered_parts[:-1]
    source_index = next(
        (i for i, part in enumerate(directory_parts) if part in SOURCE_TREE_MARKERS),
        None,
    )
    if any(
        (part in FORBIDDEN_DIR_PARTS or part.startswith("build-"))
        and (source_index is None or i < source_index)
        for i, part in enumerate(directory_parts)
    ):
        return "raw/generated/build directory"
    # Match data artifacts, not legitimate implementation or report-source
    # names such as ``restart_io.cpp`` and ``field_numbers.tex``.
    source_or_tex_suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".tex"}
    if basename in FORBIDDEN_BASENAMES or (
        RESTART_OR_FIELD.match(basename) and suffix not in source_or_tex_suffixes
    ):
        return "raw solver result or restart/field artifact"
    if basename.endswith((".run.xml", ".synctex.gz")):
        return "prohibited LaTeX build artifact"
    if suffix in {".png", ".pdf"}:
        if not any(path_text.startswith(prefix) for prefix in REPORT_FIGURE_PREFIXES):
            return f"{suffix[1:].upper()} outside an approved report figures directory"
        return None
    if suffix in FORBIDDEN_EXTENSIONS:
        return f"prohibited generated/data extension {suffix}"
    return None


def report_tex_path_for_figure(path_text: str) -> tuple[str, str] | None:
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


def _without_comments(text: str) -> str:
    return "\n".join(line.split("%", 1)[0] for line in text.splitlines())


def _report_sources(workspace: Path, commit: str, report_tex: str) -> list[tuple[str, str]]:
    """Read local TeX dependencies from the immutable commit, confined to report root."""
    report_root = posixpath.dirname(report_tex)
    pending = [report_tex]
    visited: set[str] = set()
    sources: list[tuple[str, str]] = []
    include_re = re.compile(r"\\(?:input|include)\s*\{([^{}]+)\}")
    while pending:
        current = pending.pop()
        if current in visited:
            continue
        visited.add(current)
        text = committed_text(workspace, commit, current)
        if text is None:
            continue
        text = _without_comments(text)
        sources.append((current, text))
        for match in include_re.finditer(text):
            target = match.group(1).strip().replace(r"\_", "_")
            if not target or "\\" in target or target.startswith("/"):
                continue
            if not PurePosixPath(target).suffix:
                target += ".tex"
            candidate = posixpath.normpath(posixpath.join(posixpath.dirname(current), target))
            if candidate == report_root or not candidate.startswith(report_root + "/"):
                continue
            if candidate not in visited:
                pending.append(candidate)
    return sources


def _graphics_references(sources: list[tuple[str, str]], report_root: str) -> set[str]:
    graphic_re = re.compile(
        r"\\includegraphics(?:\s*\[[^\]]*\])?\s*\{(?:\\detokenize\{([^{}]*)\}|([^{}]*))\}"
    )
    path_block_re = re.compile(r"\\graphicspath\s*\{((?:\{[^{}]*\})+)\}")
    path_item_re = re.compile(r"\{([^{}]*)\}")
    graphics_dirs: set[str] = {""}
    for _source, text in sources:
        normalized = text.replace(r"\_", "_")
        for block in path_block_re.finditer(normalized):
            for item in path_item_re.finditer(block.group(1)):
                directory = item.group(1).strip()
                if directory and not directory.startswith("/") and "\\" not in directory:
                    graphics_dirs.add(directory)

    references: set[str] = set()
    for source, text in sources:
        normalized = text.replace(r"\_", "_")
        source_dir = posixpath.dirname(source)
        for match in graphic_re.finditer(normalized):
            target = (match.group(1) or match.group(2) or "").strip()
            if not target or target.startswith("/") or "\\" in target:
                continue
            for graphics_dir in graphics_dirs:
                # TeX input files commonly retain the main report's graphics
                # search root.  In particular, generated/*.tex fragments use
                # `figures/...`, which resolves beside report.tex rather than
                # beside the generated fragment itself.
                base_dir = report_root if not graphics_dir and target.startswith("figures/") else source_dir
                candidate = posixpath.normpath(
                    posixpath.join(base_dir, graphics_dir, target)
                )
                if candidate.startswith(report_root + "/"):
                    references.add(candidate)
                    if not PurePosixPath(candidate).suffix:
                        references.add(candidate + ".png")
                        references.add(candidate + ".pdf")

        # Some reports deliberately wrap \includegraphics in a guarded macro
        # so an incomplete run renders a labelled placeholder rather than an
        # opaque TeX failure.  Follow the concrete arguments only when the
        # same source defines the conventional report-local wrapper.
        guarded = re.search(
            r"\\newcommand\s*\{\\figIfExists\}.*?\\IfFileExists\s*\{figures/#1\}"
            r".*?\\includegraphics",
            normalized,
            flags=re.DOTALL,
        )
        if guarded:
            for call in re.finditer(r"\\figIfExists\s*\{([^{}]+)\}", normalized):
                target = call.group(1).strip()
                if not target or target.startswith("/") or "\\" in target:
                    continue
                candidate = posixpath.normpath(
                    posixpath.join(report_root, "figures", target)
                )
                if candidate.startswith(report_root + "/"):
                    references.add(candidate)

        # A common report-local helper wraps \includegraphics in a four-argument
        # subfigure macro: \subp{width}{figure-stem}{caption}{label}.  The
        # second argument remains a concrete committed figure dependency even
        # though TeX expands it through \plotfile, so follow it explicitly.
        # Do this only when the same source defines the conventional helper
        # with a PNG/PDF report-figure path; arbitrary macros remain out of scope.
        helper = re.search(
            r"\\newcommand\s*\{\\(?:plotfile|subp)\}.*?figures/(?:#1|#2)\\?\.(png|pdf)",
            normalized,
            flags=re.DOTALL | re.IGNORECASE,
        )
        if helper:
            extension = helper.group(1).lower()
            for call in re.finditer(r"\\subp\s*\{[^{}]*\}\s*\{([^{}]+)\}", normalized):
                stem = call.group(1).strip()
                if not stem or stem.startswith("/") or "\\" in stem:
                    continue
                candidate = posixpath.normpath(
                    posixpath.join(source_dir, "figures", stem + "." + extension)
                )
                if candidate.startswith(report_root + "/"):
                    references.add(candidate)
    return references


def figure_is_referenced(workspace: Path, commit: str, path_text: str) -> bool:
    mapping = report_tex_path_for_figure(path_text)
    if mapping is None:
        return False
    report_tex, _relative = mapping
    sources = _report_sources(workspace, commit, report_tex)
    references = _graphics_references(sources, posixpath.dirname(report_tex))
    return path_text in references


# Backward-compatible names for evaluator scripts/tests written before PDF
# report figures were admitted.
report_tex_path_for_png = report_tex_path_for_figure
png_is_referenced = figure_is_referenced


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True)
    parser.add_argument("--submission-commit", required=True)
    parser.add_argument(
        "--contestant-checkpoint-commit",
        help="pre-evaluation HEAD; the final submission must descend from it",
    )
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
    contestant_checkpoint_commit = None
    curation_changed: list[dict[str, str]] = []
    if args.contestant_checkpoint_commit:
        contestant_checkpoint_commit = resolve_commit(
            workspace, args.contestant_checkpoint_commit
        )
        if not is_ancestor(workspace, initial_commit, contestant_checkpoint_commit):
            raise SystemExit(
                "initial commit is not an ancestor of the contestant checkpoint; "
                "resolve provenance instead of rewriting history"
            )
        if not is_ancestor(workspace, contestant_checkpoint_commit, submission_commit):
            raise SystemExit(
                "final submission must descend from the recorded contestant checkpoint "
                f"({contestant_checkpoint_commit})"
            )
        curation_changed = changed_paths(
            workspace, contestant_checkpoint_commit, submission_commit
        )

    changed = changed_paths(workspace, initial_commit, submission_commit)
    violations: list[dict[str, str]] = []
    removed_prohibited: list[dict[str, str]] = []
    for item in curation_changed:
        if not item["status"].startswith("D"):
            continue
        reason = classify(item["path"])
        if reason is not None:
            removed_prohibited.append({**item, "reason": reason})
    for item in changed:
        status = item["status"]
        path = item["path"]
        old_path = item.get("old_path")
        reason = classify(path)
        if reason is None and old_path is not None:
            old_reason = classify(old_path)
            if old_reason is not None:
                reason = "renamed inherited prohibited/data path; requires operator review: " + old_reason
        if (
            reason is None
            and path.lower().endswith((".png", ".pdf"))
            and not status.startswith("D")
            and not figure_is_referenced(workspace, submission_commit, path)
        ):
            reason = "report figure is not referenced by committed report.tex"
        if reason is not None:
            if status.startswith("D"):
                cleanup = {**item, "reason": reason}
                if cleanup not in removed_prohibited:
                    removed_prohibited.append(cleanup)
            else:
                violations.append({**item, "reason": reason})

    result = {
        "initial_commit": initial_commit,
        "contestant_checkpoint_commit": contestant_checkpoint_commit,
        "submission_commit": submission_commit,
        "initial_is_ancestor_of_checkpoint": (
            True if contestant_checkpoint_commit is not None else None
        ),
        "checkpoint_is_submission_ancestor": (
            True if contestant_checkpoint_commit is not None else None
        ),
        "curation_changed_count": len(curation_changed),
        "curation_changed": curation_changed,
        "changed_count": len(changed),
        "changed": changed,
        "removed_prohibited": removed_prohibited,
        "violations": violations,
        "passed": not violations,
        "note": (
            "Final-tip pattern audit only; preserved ancestor commits are not rewritten. "
            "Evaluator must inspect every allowed path and record that earlier commits may "
            "retain artifacts removed by the curation commit."
        ),
    }
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Local web GUI for the evaluation snapshot database.

Serves the snapshot table (key results) and interactive detail views over
HTTP. Stdlib only:

  python3 evaluation/gui/server.py [--port 8787] [--outputs evaluation/outputs]

Endpoints:
  GET /api/snapshots              table rows, wrapped as {"snapshots": [...]}
  GET /api/snapshot/<contestant>   full detail payload
  GET /api/snapshot/<contestant>/file/<artifact>   raw artifact (md/json)
  GET /api/snapshot/<contestant>/report-pdf       report PDF from matching workspace
  GET /                           static GUI (index.html)

The GUI is intentionally read-only: it never writes to snapshots or workspaces.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANAGER_ROOT = ROOT.parent
WORKSPACE_ROOT = MANAGER_ROOT / "workspace"
sys.path.insert(0, str(ROOT / "src"))
from cfdeval import query  # noqa: E402


SAFE_ARTIFACTS = (
    "summary.md", "summary.json", "metadata.json", "expenses.json",
    "measurements.json", "sessions.json", "configs.json",
    "env_snapshot.json", "agent_scores.json", "agent_report.md",
    "review_code.md", "review_code.json", "review_cfd.md", "review_cfd.json",
    "review_results.md", "review_results.json", "index.json",
    "run_identity.json", "contestant_final_response.md",
)


def _inside(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except (OSError, ValueError):
        return False


def workspace_for_snapshot(run_identity: dict | None) -> Path | None:
    """Resolve a contestant repo without allowing arbitrary filesystem access."""
    identity = run_identity or {}
    candidates = []
    recorded = identity.get("workspace")
    if isinstance(recorded, str):
        candidates.append(Path(recorded))
    branch, number = identity.get("initial_branch"), identity.get("operator_number")
    if isinstance(branch, str) and isinstance(number, str):
        parts = branch.split("/")
        if len(parts) == 3 and parts[-1] == "init":
            candidates.append(WORKSPACE_ROOT / parts[0] / parts[1] / number)
    for candidate in candidates:
        if _inside(candidate, WORKSPACE_ROOT) and candidate.resolve().is_dir():
            return candidate.resolve()
    return None


def _tracked_report_tex_paths(workspace: Path, run_identity: dict | None) -> list[Path]:
    """Return report-like TeX sources tracked by the immutable submission."""
    identity = run_identity or {}
    submission = identity.get("submission_commit")
    if not isinstance(submission, str) or not submission:
        return []
    try:
        proc = subprocess.run(
            ["git", "ls-tree", "-r", "--name-only", submission],
            cwd=workspace, check=True, capture_output=True, text=True, timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    candidates = []
    for raw in proc.stdout.splitlines():
        rel = Path(raw)
        lowered = [part.lower() for part in rel.parts]
        if rel.suffix.lower() != ".tex":
            continue
        if rel.name.lower() != "report.tex" and "report" not in lowered[:-1]:
            continue
        path = workspace / rel
        if _inside(path, workspace):
            candidates.append(rel)

    contents: dict[Path, str] = {}
    referenced: set[Path] = set()
    candidate_set = set(candidates)
    for rel in candidates:
        try:
            blob = subprocess.run(
                ["git", "show", f"{submission}:{rel.as_posix()}"],
                cwd=workspace, check=True, capture_output=True, text=True, timeout=5,
            ).stdout
        except (OSError, subprocess.SubprocessError):
            blob = ""
        contents[rel] = blob
        for include in re.findall(r"\\(?:input|include)\s*\{([^}]+)\}", blob):
            dependency = rel.parent / include
            if dependency.suffix.lower() != ".tex":
                dependency = dependency.with_suffix(".tex")
            if dependency in candidate_set:
                referenced.add(dependency)

    roots = [rel for rel in candidates
             if rel.name.lower() == "report.tex"
             or "\\documentclass" in contents.get(rel, "")
             or rel not in referenced]
    return sorted(roots, key=lambda rel: (
        0 if rel.name.lower() == "report.tex" else 1,
        0 if "\\documentclass" in contents.get(rel, "") else 1,
        0 if rel.parent.name.lower() == "report" else 1,
        len(rel.parts), rel.as_posix(),
    ))


def report_pdf_search(workspace: Path | None, run_identity: dict | None = None) -> dict:
    """Resolve the report PDF and retain useful diagnostics when it is absent."""
    if workspace is None:
        return {"report": None, "workspace_found": False, "expected_paths": [],
                "tracked_tex_paths": []}

    tex_paths = _tracked_report_tex_paths(workspace, run_identity)
    expected = [rel.with_suffix(".pdf") for rel in tex_paths]
    found: list[tuple[tuple, Path, str, int]] = []
    for rank, rel in enumerate(expected):
        path = workspace / rel
        if path.is_symlink() or not _inside(path, workspace) or not path.is_file():
            continue
        try:
            size = path.stat().st_size
        except OSError:
            continue
        found.append(((0, rank), path, rel.as_posix(), size))

    # Legacy snapshots may lack a usable submission commit. Keep the fallback
    # deliberately narrow: arbitrary plot PDFs are not contestant reports.
    skip = {
        ".git", ".sessions", ".eval", ".venv", "build", "build-debug",
        "build-release", "cfd_solver_agentic_benchmark", "external",
    }
    for root, dirs, files in os.walk(workspace, followlinks=False):
        dirs[:] = sorted(d for d in dirs if d not in skip and not d.startswith("cmake-build"))
        root_path = Path(root)
        for name in sorted(files):
            if name.lower() != "report.pdf":
                continue
            path = root_path / name
            if path.is_symlink() or not _inside(path, workspace):
                continue
            try:
                size = path.stat().st_size
            except OSError:
                continue
            rel = path.relative_to(workspace).as_posix()
            parts = [p.lower() for p in path.relative_to(workspace).parts]
            score = (1, 0 if "report" in parts[:-1] else 1, len(parts), rel)
            found.append((score, path, rel, size))
    if not found:
        return {
            "report": None,
            "workspace_found": True,
            "workspace": str(workspace),
            "expected_paths": [rel.as_posix() for rel in expected],
            "tracked_tex_paths": [rel.as_posix() for rel in tex_paths],
        }
    _, path, rel, size = min(found, key=lambda item: item[0])
    return {
        "report": {"path": path, "relative_path": rel, "bytes": size},
        "workspace_found": True,
        "workspace": str(workspace),
        "expected_paths": [rel.as_posix() for rel in expected],
        "tracked_tex_paths": [rel.as_posix() for rel in tex_paths],
    }


def find_report_pdf(workspace: Path | None, run_identity: dict | None = None) -> dict | None:
    """Find the report PDF tied to a submitted report source."""
    return report_pdf_search(workspace, run_identity)["report"]


def snapshot_detail(folder: Path) -> dict:
    """Assemble the detail payload for one snapshot folder."""
    def _load(name):
        p = folder / name
        if not p.exists():
            return None
        try:
            return json.loads(p.read_text())
        except (OSError, json.JSONDecodeError):
            return None

    summary = _load("summary.json") or {}
    sessions = _load("sessions.json")
    metadata = _load("metadata.json")
    if metadata:
        metadata = dict(metadata)
        normalized_status = query.effective_metadata_status(metadata)
        if normalized_status is not None:
            metadata["status"] = normalized_status
    expenses = _load("expenses.json")
    agent_scores = _load("agent_scores.json")
    configs = _load("configs.json")
    env_snap = _load("env_snapshot.json")
    run_identity = _load("run_identity.json")
    report_search = report_pdf_search(workspace_for_snapshot(run_identity), run_identity)
    report_pdf = report_search["report"]
    expense_facts = expenses or summary.get("expenses") or {}
    metadata_facts = metadata or summary.get("metadata") or {}
    model_decomposition = query.current_model_decomposition(
        expense_facts, metadata_facts, agent_scores)
    current_cost_estimate = query.current_snapshot_cost(
        expense_facts, metadata_facts, agent_scores,
        decomposition=model_decomposition)
    md_files = {}
    for name in ("summary.md", "agent_report.md", "contestant_final_response.md",
                 "review_code.md", "review_cfd.md", "review_results.md"):
        p = folder / name
        if p.exists():
            md_files[name] = p.read_text(encoding="utf-8", errors="replace")[:500_000]
    return {
        "name": folder.name,
        "contestant": summary.get("contestant"),
        "snapshot": summary.get("snapshot", {}),
        "summary": summary,
        "current_cost_estimate": current_cost_estimate,
        "model_decomposition": model_decomposition,
        "sessions": sessions,
        "metadata": metadata,
        "agent_scores": agent_scores,
        "configs": {
            "captured_at": (configs or {}).get("captured_at"),
            "count": len((configs or {}).get("configs", [])),
            "redaction_hits": ((configs or {}).get("redaction") or {}).get("total_hits"),
            "roles": sorted({c.get("role") for c in (configs or {}).get("configs", [])}),
            "files": [
                {"role": c.get("role"), "path": c.get("path"), "exists": c.get("exists"),
                 "content_included": c.get("content_included"),
                 "redacted": c.get("redacted"), "bytes": c.get("bytes"),
                 "content": c.get("content")}
                for c in (configs or {}).get("configs", [])
            ],
        },
        "env_snapshot": env_snap,
        "environment_capture_phase": query.environment_capture_phase(env_snap),
        "run_identity": run_identity,
        "report_pdf": ({"relative_path": report_pdf["relative_path"],
                        "bytes": report_pdf["bytes"],
                        "url": f"/api/snapshot/{urllib.parse.quote(folder.name, safe='')}/report-pdf"}
                       if report_pdf else None),
        "report_pdf_search": {key: value for key, value in report_search.items()
                              if key != "report"},
        "markdown": md_files,
        "artifacts": sorted(
            p.name for p in folder.iterdir() if p.is_file() and p.name in SAFE_ARTIFACTS),
        "has_agent_report": (folder / "agent_report.md").exists(),
        "has_agent_scores": (folder / "agent_scores.json").exists(),
        "env_captured": (folder / "env_snapshot.json").exists(),
    }


class Handler(BaseHTTPRequestHandler):
    outputs_root: Path = ROOT / "outputs"

    def _json(self, obj, status: int = 200) -> None:
        data = json.dumps(obj, indent=2).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _file(self, path: Path, content_type: str) -> None:
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _not_found(self, msg: str = "not found") -> None:
        self._json({"error": msg}, 404)

    def do_GET(self):  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        try:
            if path == "/api/snapshots":
                rows = []
                for folder in query.result_folders(self.outputs_root):
                    try:
                        r = query.load(folder)
                        rows.append(query.row_for(folder, r["summary"]))
                    except (OSError, json.JSONDecodeError):
                        continue
                return self._json({"snapshots": rows})
            if path.startswith("/api/snapshot/"):
                rest = path[len("/api/snapshot/"):].split("/")
                if len(rest) == 1:
                    folder = (self.outputs_root / rest[0]).resolve()
                    if (not _inside(folder, self.outputs_root) or not folder.is_dir()
                            or not (folder / "index.json").exists()):
                        return self._not_found(f"snapshot {rest[0]}")
                    return self._json(snapshot_detail(folder))
                if len(rest) == 3 and rest[1] == "file":
                    artifact = urllib.parse.unquote(rest[2])
                    if artifact not in SAFE_ARTIFACTS:
                        return self._not_found("artifact not allowed")
                    fp = (self.outputs_root / rest[0] / artifact).resolve()
                    if not _inside(fp, self.outputs_root) or not fp.is_file():
                        return self._not_found("artifact missing")
                    ctype = ("text/markdown; charset=utf-8"
                             if artifact.endswith(".md") else
                             "application/json; charset=utf-8")
                    return self._file(fp, ctype)
                if len(rest) == 2 and rest[1] == "report-pdf":
                    folder = (self.outputs_root / rest[0]).resolve()
                    if (not _inside(folder, self.outputs_root) or not folder.is_dir()
                            or not (folder / "index.json").exists()):
                        return self._not_found(f"snapshot {rest[0]}")
                    try:
                        identity = json.loads((folder / "run_identity.json").read_text())
                    except (OSError, json.JSONDecodeError):
                        identity = None
                    report = find_report_pdf(workspace_for_snapshot(identity), identity)
                    if not report:
                        return self._not_found("workspace report PDF")
                    return self._file(report["path"], "application/pdf")
            if path in ("/", "/index.html"):
                static = Path(__file__).resolve().parent / "static" / "index.html"
                if not static.exists():
                    return self._not_found("GUI static files not built yet")
                return self._file(static, "text/html; charset=utf-8")
            if path.startswith("/static/"):
                name = urllib.parse.unquote(path[len("/static/"):])
                fp = (Path(__file__).resolve().parent / "static" / name).resolve()
                if not str(fp).startswith(str(Path(__file__).resolve().parent / "static")):
                    return self._not_found("bad path")
                if not fp.is_file():
                    return self._not_found("static file missing")
                ctype = {
                    ".js": "text/javascript; charset=utf-8",
                    ".css": "text/css; charset=utf-8",
                    ".html": "text/html; charset=utf-8",
                }.get(fp.suffix, "application/octet-stream")
                return self._file(fp, ctype)
            return self._not_found(f"no route for {path}")
        except (OSError, ValueError) as exc:
            return self._json({"error": str(exc)}, 500)

    def log_message(self, fmt, *args):  # quiet default logging
        sys.stderr.write("[gui] " + fmt % args + "\n")


def main(argv: list[str] | None = None) -> int:
    global WORKSPACE_ROOT
    ap = argparse.ArgumentParser(description="Evaluation snapshot GUI server")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--outputs", default=str(ROOT / "outputs"))
    ap.add_argument("--workspace-root", default=str(WORKSPACE_ROOT),
                    help="root containing workspace/<harness>/<model>/<number> repos")
    args = ap.parse_args(argv)
    Handler.outputs_root = Path(args.outputs).resolve()
    WORKSPACE_ROOT = Path(args.workspace_root).resolve()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"evaluation GUI on http://{args.host}:{args.port} "
          f"(snapshots: {Handler.outputs_root})")
    print("ctrl-c to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

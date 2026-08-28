"""Vendor an evaluator-approved contestant report PDF into a snapshot."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


REPORT_PDF = "report.pdf"
REPORT_METADATA = "report_pdf.json"
MEDIA_TYPE = "application/pdf"
MAX_REPORT_BYTES = 100 * 1024 * 1024


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _inside(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except (OSError, ValueError):
        return False


def normalized_repo_path(value: str) -> str:
    rel = PurePosixPath(value)
    if (not value or rel.is_absolute() or "\\" in value or ".." in rel.parts
            or rel.as_posix() != value):
        raise ValueError(f"path must be normalized and repository-relative: {value!r}")
    return value


def validate_pdf_bytes(data: bytes) -> list[str]:
    errors = []
    if len(data) < 8 or not data.startswith(b"%PDF-"):
        errors.append("missing PDF header")
    if b"%%EOF" not in data[-4096:]:
        errors.append("missing PDF EOF marker in final 4096 bytes")
    if len(data) > MAX_REPORT_BYTES:
        errors.append(f"PDF exceeds {MAX_REPORT_BYTES} byte snapshot limit")
    return errors


def validate_pdf_file(path: Path) -> tuple[bytes, list[str]]:
    if path.is_symlink():
        return b"", ["PDF source may not be a symlink"]
    if not path.is_file():
        return b"", ["PDF source is not a regular file"]
    try:
        data = path.read_bytes()
    except OSError as exc:
        return b"", [f"cannot read PDF source: {exc}"]
    return data, validate_pdf_bytes(data)


def _tracked_tex(workspace: Path, submission_commit: str, source_tex: str) -> None:
    normalized_repo_path(source_tex)
    if not source_tex.lower().endswith(".tex"):
        raise ValueError("--source-tex must identify a TeX source")
    proc = subprocess.run(
        ["git", "ls-tree", submission_commit, "--", f":(literal){source_tex}"],
        cwd=workspace, capture_output=True, text=True, timeout=10,
    )
    fields = proc.stdout.strip().split(None, 3) if proc.returncode == 0 else []
    if len(fields) != 4 or fields[1] != "blob" or fields[0] not in {"100644", "100755"}:
        raise ValueError(
            f"report TeX is not a tracked blob in submission {submission_commit}: "
            f"{source_tex}")


def record_accepted(
    *, snapshot: Path, workspace: Path, source: Path, source_mode: str,
    source_tex: str, submission_commit: str, evaluator: str, notes: str,
    build_command: str | None = None, visually_reviewed: bool = False,
    main_report_confirmed: bool = False, readable: bool = False,
) -> dict:
    """Copy one manually approved main report and write its provenance record."""
    snapshot = snapshot.resolve()
    workspace = workspace.resolve()
    source_input = source
    if source_mode not in {"workspace_existing", "compiled_from_submission"}:
        raise ValueError(f"unsupported source mode: {source_mode}")
    if not evaluator.strip() or not notes.strip():
        raise ValueError("evaluator and appropriateness notes are required")
    if not (visually_reviewed and main_report_confirmed and readable):
        raise ValueError(
            "accepted report requires visual review, main-report confirmation, and readability")
    if not submission_commit.strip():
        raise ValueError("submission commit is required")
    _tracked_tex(workspace, submission_commit, source_tex)
    if source_mode == "workspace_existing":
        if not _inside(source_input, workspace):
            raise ValueError("workspace-existing report PDF must be inside workspace")
        expected = (workspace / PurePosixPath(source_tex)).with_suffix(".pdf").resolve()
        if source_input.resolve(strict=False) != expected:
            raise ValueError(
                "workspace-existing PDF must be the sibling compiled output of --source-tex")
    elif not (build_command or "").strip():
        raise ValueError("compiled-from-submission PDF requires --build-command")

    data, errors = validate_pdf_file(source_input)
    if errors:
        raise ValueError("invalid report PDF: " + "; ".join(errors))
    snapshot.mkdir(parents=True, exist_ok=True)
    destination = snapshot / REPORT_PDF
    if source_input.resolve(strict=False) == destination.resolve(strict=False):
        raise ValueError("source PDF may not be the snapshot destination")
    with tempfile.NamedTemporaryFile(
            prefix=f".{REPORT_PDF}.", suffix=".tmp", dir=snapshot,
            delete=False) as handle:
        handle.write(data)
        tmp = Path(handle.name)
    os.replace(tmp, destination)
    digest = sha256_bytes(data)
    source_record = {
        "mode": source_mode,
        "report_tex": source_tex,
        "sha256": digest,
        "bytes": len(data),
        "workspace_relative_pdf": (
            source_input.resolve().relative_to(workspace).as_posix()
            if source_mode == "workspace_existing" else None),
        "build_command": build_command if source_mode == "compiled_from_submission" else None,
    }
    record = {
        "schema_version": "1.0",
        "status": "accepted",
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "evaluator": evaluator.strip(),
        "submission_commit": submission_commit,
        "source": source_record,
        "snapshot": {
            "filename": REPORT_PDF,
            "media_type": MEDIA_TYPE,
            "sha256": digest,
            "bytes": len(data),
            "pdf_header_valid": True,
            "pdf_eof_valid": True,
        },
        "appropriateness": {
            "approved": True,
            "visually_reviewed": True,
            "main_report_confirmed": True,
            "readable": True,
            "notes": notes.strip(),
        },
    }
    metadata_data = (json.dumps(record, indent=2) + "\n").encode()
    with tempfile.NamedTemporaryFile(
            prefix=f".{REPORT_METADATA}.", suffix=".tmp", dir=snapshot,
            delete=False) as handle:
        handle.write(metadata_data)
        metadata_tmp = Path(handle.name)
    os.replace(metadata_tmp, snapshot / REPORT_METADATA)
    return record


def record_absent(*, snapshot: Path, evaluator: str, reason: str,
                  submission_commit: str | None = None) -> dict:
    """Record that no appropriate main report PDF is available."""
    if not evaluator.strip() or not reason.strip():
        raise ValueError("evaluator and absence reason are required")
    if not (submission_commit or "").strip():
        raise ValueError("submission commit is required for an absence record")
    snapshot = snapshot.resolve()
    snapshot.mkdir(parents=True, exist_ok=True)
    pdf = snapshot / REPORT_PDF
    if pdf.exists() or pdf.is_symlink():
        raise ValueError("remove stale snapshot report.pdf before recording absence")
    record = {
        "schema_version": "1.0",
        "status": "absent",
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "evaluator": evaluator.strip(),
        "submission_commit": submission_commit,
        "source": None,
        "snapshot": None,
        "appropriateness": {
            "approved": False,
            "visually_reviewed": False,
            "main_report_confirmed": False,
            "readable": False,
            "notes": reason.strip(),
        },
    }
    metadata_data = (json.dumps(record, indent=2) + "\n").encode()
    with tempfile.NamedTemporaryFile(
            prefix=f".{REPORT_METADATA}.", suffix=".tmp", dir=snapshot,
            delete=False) as handle:
        handle.write(metadata_data)
        metadata_tmp = Path(handle.name)
    os.replace(metadata_tmp, snapshot / REPORT_METADATA)
    return record

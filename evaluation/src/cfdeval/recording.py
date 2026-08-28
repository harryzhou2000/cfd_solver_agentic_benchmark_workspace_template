"""Standardized recording of evaluation result folders: index.json manifest
with schema references and sha256 digests."""

from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


CONTRACT_VERSION = "1.1"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_index(folder: Path, contestant: str, artifacts: dict[str, str | None],
                tools: list[str], schema_dir: Path) -> Path:
    """Write <folder>/index.json. `artifacts` maps artifact filename ->
    schema filename (relative to schema_dir)."""
    index = {
        "contract_version": CONTRACT_VERSION,
        "contestant": contestant,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "tools": tools,
        "artifacts": {},
    }
    for name, schema in sorted(artifacts.items()):
        if name == "index.json":
            continue  # never digest the manifest itself
        rel = PurePosixPath(name)
        if (not name or rel.is_absolute() or ".." in rel.parts
                or len(rel.parts) != 1 or rel.as_posix() != name):
            raise ValueError(f"artifact name must be one safe top-level filename: {name!r}")
        f = folder / name
        if f.exists():
            if f.is_symlink() or not f.is_file():
                raise ValueError(f"indexed artifact must be a regular non-symlink file: {name}")
            item = {
                "artifact_kind": "json" if schema is not None else "binary",
                "schema": schema,
                "sha256": sha256_bytes(f.read_bytes()),
                "bytes": f.stat().st_size,
            }
            if schema is None:
                if f.suffix.lower() == ".pdf":
                    item["media_type"] = "application/pdf"
                else:
                    raise ValueError(
                        f"schema-less indexed artifact has unsupported type: {name}")
            index["artifacts"][name] = item
    path = folder / "index.json"
    path.write_text(json.dumps(index, indent=2) + "\n")
    return path


def read_result(folder: Path) -> dict:
    """Load a contract result folder: index + summary."""
    index = json.loads((folder / "index.json").read_text())
    summary = json.loads((folder / "summary.json").read_text())
    return {"index": index, "summary": summary}

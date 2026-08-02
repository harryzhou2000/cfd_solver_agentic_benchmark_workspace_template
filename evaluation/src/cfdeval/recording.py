"""Standardized recording of evaluation result folders: index.json manifest
with schema references and sha256 digests."""

from __future__ import annotations

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


CONTRACT_VERSION = "1.0"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_index(folder: Path, contestant: str, artifacts: dict[str, str],
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
        f = folder / name
        if f.exists():
            index["artifacts"][name] = {
                "schema": schema,
                "sha256": sha256_bytes(f.read_bytes()),
                "bytes": f.stat().st_size,
            }
    path = folder / "index.json"
    path.write_text(json.dumps(index, indent=2) + "\n")
    return path


def read_result(folder: Path) -> dict:
    """Load a contract result folder: index + summary."""
    index = json.loads((folder / "index.json").read_text())
    summary = json.loads((folder / "summary.json").read_text())
    return {"index": index, "summary": summary}

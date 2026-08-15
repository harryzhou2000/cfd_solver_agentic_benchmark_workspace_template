#!/usr/bin/env python3
"""Set metadata.json git_revision to the source revision used for results.

The submitted result directories were produced with the release executable
built from a specific source revision. This script rewrites the
git_revision field in every results/*/metadata.json so the provenance field
points at that revision instead of an earlier configure-time value.

Usage: update_metadata_revision.py [revision] [results-dir]
"""

import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    results = Path(sys.argv[2] if len(sys.argv) > 2 else "results")
    revision = sys.argv[1] if len(sys.argv) > 1 else ""
    if not revision:
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], text=True
        ).strip()
    changed = 0
    for meta in sorted(results.glob("*/metadata.json")):
        data = json.loads(meta.read_text())
        if data.get("git_revision") == revision:
            continue
        data["git_revision"] = revision
        meta.write_text(json.dumps(data, indent=2) + "\n")
        print(f"updated {meta} -> {revision}")
        changed += 1
    print(f"{changed} metadata file(s) updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())

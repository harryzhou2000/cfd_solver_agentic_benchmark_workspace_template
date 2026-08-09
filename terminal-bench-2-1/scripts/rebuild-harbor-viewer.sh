#!/usr/bin/env bash
# Rebuild the harbor viewer frontend and install it into the uv tool env.
# Run after every `uv tool install --force` — the wheel does not ship the
# React build (it lives in harbor/apps/viewer/build/client).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT/harbor/apps/viewer"

if ! command -v bun >/dev/null 2>&1; then
  echo "bun is required to build the viewer frontend" >&2
  exit 1
fi

bun install
bun run build

HB_PY="$(uv tool dir)/harbor/bin/python"
SITE="$(dirname "$("$HB_PY" -c 'import harbor, os; print(os.path.abspath(harbor.__file__))')")"
mkdir -p "$SITE/viewer/static"
cp -r build/client/. "$SITE/viewer/static/"

echo "Viewer frontend installed -> $SITE/viewer/static"

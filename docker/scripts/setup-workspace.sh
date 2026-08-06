#!/usr/bin/env bash
# Create a fresh contestant workspace from the template repo and run the
# standard setup sequence:
#   git remote rm origin && codegraph init && ln -s <external> .
#
# Usage:
#   docker/scripts/setup-workspace.sh <path> [branch]
# <path> is used as-is when absolute; relative paths resolve under the
# manager repo's workspace/ directory (override with WS_ROOT):
#   docker/scripts/setup-workspace.sh codex/gpt56/08 codex/gpt56/init
#   -> <repo>/workspace/codex/gpt56/08
#   docker/scripts/setup-workspace.sh /abs/path/omo_slim_dsv4_06 omo_slim/dsv4/init
# (branch defaults to main; workspace/ is git-ignored)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

WS_ARG="${1:?usage: setup-workspace.sh <path> [branch]}"
BRANCH="${2:-main}"
WS_ROOT="${WS_ROOT:-$ROOT/workspace}"
BENCH_ROOT="${BENCH_ROOT:-/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark}"
TEMPLATE_URL="${TEMPLATE_URL:-https://github.com/harryzhou2000/cfd_solver_agentic_benchmark_workspace_template.git}"
# The externals are built into the benchmark image (/opt/external) via the
# DNDSR process — no host binary tree is needed. Override EXTERNAL_SRC for a
# custom source (e.g. a local checkout on this host).
EXTERNAL_SRC="${EXTERNAL_SRC:-/opt/external}"

case "$WS_ARG" in
  /*) WS="$WS_ARG" ;;
  ~)  WS="$HOME" ;;
  ~/*) WS="$HOME/${WS_ARG#\~}" ;;
  *)  WS="$WS_ROOT/$WS_ARG" ;;
esac
WS="$(realpath -m "$WS")"

if [ -e "$WS" ]; then
  echo "error: $WS already exists" >&2
  exit 1
fi

echo "== cloning template branch $BRANCH -> $WS =="
mkdir -p "$(dirname "$WS")"
git clone --branch "$BRANCH" --single-branch "$TEMPLATE_URL" "$WS"
cd "$WS"

echo "== pinning benchmark submodule =="
git submodule update --init --recursive

echo "== setup: .sessions/ (bundled codex/opencode sessions, git-excluded) =="
mkdir -p .sessions
grep -qxF '.sessions/' .git/info/exclude 2>/dev/null || printf '.sessions/\n' >> .git/info/exclude
grep -qxF '.opencode/' .git/info/exclude 2>/dev/null || printf '.opencode/\n' >> .git/info/exclude
grep -qxF '.eval/' .git/info/exclude 2>/dev/null || printf '.eval/\n' >> .git/info/exclude

echo "== setup: git remote rm origin =="
git remote rm origin

echo "== setup: codegraph init =="
if command -v codegraph >/dev/null 2>&1; then
  codegraph init
else
  echo "codegraph not found on host; run 'codegraph init' inside the container"
fi

echo "== setup: ln -s $EXTERNAL_SRC . =="
if [ -d "$EXTERNAL_SRC" ] || [ "$EXTERNAL_SRC" = "/opt/external" ]; then
  ln -s "$EXTERNAL_SRC" external
  [ -d "$EXTERNAL_SRC" ] || echo "  (image-built externals; /opt/external resolves inside the container)"
else
  echo "warning: external source $EXTERNAL_SRC not found; create the symlink manually" >&2
fi

echo "== setup: environment snapshot (optional; captured before the agent runs) =="
if command -v python3 >/dev/null 2>&1; then
  ENV_SNAP="$ROOT/evaluation/tools/env_snapshot.py"
  if [ -f "$ENV_SNAP" ]; then
    if python3 "$ENV_SNAP" --workspace "$WS" --probe-proxy; then
      echo "  (env snapshot written to $WS/.eval/env_snapshot.json; the evaluation"
      echo "   pipeline copies it into the result snapshot as env_snapshot.json)"
    else
      echo "  (env snapshot failed; the run continues without it — it is optional)"
    fi
  else
    echo "  (env_snapshot.py not found in this template revision — skipping)"
  fi
else
  echo "  (python3 not found — env snapshot skipped; it is optional)"
fi

echo
echo "workspace ready: $WS"
echo "launch:  docker/scripts/start.sh --workspace $WS"

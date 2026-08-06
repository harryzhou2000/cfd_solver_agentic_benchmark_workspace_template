#!/usr/bin/env bash
# Create a fresh contestant workspace from the template repo and run the
# standard setup sequence:
#   git remote rm origin && codegraph init && ln -s <external> .
#
# Usage:
#   docker/scripts/setup-workspace.sh <path> [branch]
# <path> is used as-is: absolute, or relative to the directory the script is
# invoked from (no $BENCH_ROOT prefixing). From the benchmark root:
#   docker/scripts/setup-workspace.sh ../codex_gpt56_07 codex/gpt56/init
#   docker/scripts/setup-workspace.sh /abs/path/omo_slim_dsv4_06 omo_slim/dsv4/init
# (branch defaults to main)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
INVOKE_CWD="$PWD"
cd "$ROOT"

WS_ARG="${1:?usage: setup-workspace.sh <path> [branch]}"
BRANCH="${2:-main}"
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
  *)  WS="$INVOKE_CWD/$WS_ARG" ;;
esac
WS="$(realpath -m "$WS")"

if [ -e "$WS" ]; then
  echo "error: $WS already exists" >&2
  exit 1
fi

echo "== cloning template branch $BRANCH -> $WS =="
git clone --branch "$BRANCH" --single-branch "$TEMPLATE_URL" "$WS"
cd "$WS"

echo "== pinning benchmark submodule =="
git submodule update --init --recursive

echo "== setup: .sessions/ (bundled codex/opencode sessions, git-excluded) =="
mkdir -p .sessions
grep -qxF '.sessions/' .git/info/exclude 2>/dev/null || printf '.sessions/\n' >> .git/info/exclude
grep -qxF '.opencode/' .git/info/exclude 2>/dev/null || printf '.opencode/\n' >> .git/info/exclude

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

echo
echo "workspace ready: $WS"
echo "launch:  docker/scripts/start.sh --workspace $WS"

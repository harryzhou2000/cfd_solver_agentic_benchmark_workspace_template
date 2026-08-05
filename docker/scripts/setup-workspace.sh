#!/usr/bin/env bash
# Create a fresh contestant workspace from the template repo and run the
# standard setup sequence:
#   git remote rm origin && codegraph init && ln -s ../opencode_omoslim_deepseek/external .
#
# Usage:
#   docker/scripts/setup-workspace.sh <name> [branch]
# (branch defaults to main; e.g. codex/gpt56/init, omo_slim/dsv4/init)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

NAME="${1:?usage: setup-workspace.sh <name> [branch]}"
BRANCH="${2:-main}"
BENCH_ROOT="${BENCH_ROOT:-/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark}"
TEMPLATE_URL="${TEMPLATE_URL:-https://github.com/harryzhou2000/cfd_solver_agentic_benchmark_workspace_template.git}"

WS="$BENCH_ROOT/$NAME"
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

echo "== setup: git remote rm origin =="
git remote rm origin

echo "== setup: codegraph init =="
if command -v codegraph >/dev/null 2>&1; then
  codegraph init
else
  echo "codegraph not found on host; run 'codegraph init' inside the container"
fi

echo "== setup: ln -s ../opencode_omoslim_deepseek/external . =="
ln -s ../opencode_omoslim_deepseek/external .

echo
echo "workspace ready: $WS"
echo "launch:  docker/scripts/start.sh --workspace $WS"

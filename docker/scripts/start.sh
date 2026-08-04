#!/usr/bin/env bash
# Interactive launcher for a benchmark contestant container.
#
# Usage:
#   docker/scripts/start.sh [--workspace DIR] [--image-config] [--harness shell|codex|opencode] [-- cmd...]
#
# Mounts (same absolute paths as the host):
#   - benchmark root (parent of the contestant workspaces)
#   - ~/.codex, ~/.config/opencode, ~/.local/share/opencode,
#     ~/.opencodex, ~/.codegraph
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

IMAGE="${IMAGE:-cfd-bench:latest}"
BENCH_ROOT="${BENCH_ROOT:-/mnt/ssd-SATARAID5/harry/projects/cfd_agentic_benchmark}"
WS=""
MOUNT_CONFIG=1
HARNESS="shell"

while [ $# -gt 0 ]; do
  case "$1" in
    --workspace) WS="$2"; shift 2 ;;
    --image-config) MOUNT_CONFIG=0; shift ;;
    --harness) HARNESS="$2"; shift 2 ;;
    --) shift; break ;;
    *) WS="${WS:-$1}"; shift ;;
  esac
done

WS="$(realpath "${WS:-$PWD}")"
if [ ! -d "$WS" ]; then
  echo "workspace $WS does not exist" >&2
  exit 1
fi

case "$HARNESS" in
  shell) CMD=("/bin/zsh") ;;
  codex) CMD=("codex") ;;
  opencode) CMD=("opencode") ;;
  *) echo "unknown --harness $HARNESS (shell|codex|opencode)" >&2; exit 1 ;;
esac
if [ $# -gt 0 ]; then CMD=("$@"); fi

MOUNTS=(-v "$BENCH_ROOT:$BENCH_ROOT")
if [ "$MOUNT_CONFIG" = "1" ]; then
  for d in .codex .config/opencode .local/share/opencode .opencodex .codegraph; do
    mkdir -p "$HOME/$d"
    MOUNTS+=(-v "$HOME/$d:$HOME/$d")
  done
else
  echo "using baked (redacted) configs instead of live host configs"
fi

echo "== container =="
echo "  image:    $IMAGE"
echo "  workspace: $WS"
echo "  harness:  $HARNESS"
echo "  mounts:   ${MOUNTS[*]}"
echo

exec docker run -it --rm --network host --name "bench-$(basename "$WS")" \
  --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" \
  "${MOUNTS[@]}" \
  -w "$WS" \
  "$IMAGE" "${CMD[@]}"

#!/usr/bin/env bash
# Stage host artifacts into docker/.context (hardlinks when possible), then
# build the image. Run from the repo root.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

IMAGE="${IMAGE:-cfd-bench:latest}"
C="$ROOT/docker/.context"
mkdir -p "$C"

stage() {
  local src="$1" dst="$2"
  if [ -e "$src" ]; then
    rm -rf "$dst"
    if [ -d "$src" ]; then
      mkdir -p "$dst"
      if ! cp -al "$src"/. "$dst"/ 2>/dev/null; then
        echo "  (hardlink stage failed for $src; falling back to copy)"
        rm -rf "$dst" && mkdir -p "$dst"
        cp -a "$src"/. "$dst"/
      fi
    else
      if ! cp -al "$src" "$dst" 2>/dev/null; then
        echo "  (hardlink stage failed for $src; falling back to copy)"
        cp -a "$src" "$dst"
      fi
    fi
    echo "staged $(du -sh "$dst" | cut -f1)  $dst"
  else
    echo "WARNING: missing $src (skipped)" >&2
  fi
}

echo "== staging host artifacts =="
stage "$HOME/.opencode/bin/opencode"                       "$C/opencode"
stage "$HOME/.codex/packages/standalone/releases/0.146.0-x86_64-unknown-linux-musl" "$C/codex-release"
stage "$HOME/.codegraph/versions/v1.2.0"                   "$C/codegraph"
stage "/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install" "$C/openmpi"
stage "$ROOT/../opencode_omoslim_deepseek/external"        "$C/external"
stage "$HOME/.local/share/opencode-goal-plugin"            "$C/opencode-goal-plugin"

echo "== docker build =="
docker build -f docker/Dockerfile -t "$IMAGE" .
echo "built $IMAGE"

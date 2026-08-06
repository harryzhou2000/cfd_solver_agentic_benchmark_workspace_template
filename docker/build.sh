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

echo "== staging live user configs (real, gitignored) =="
CC="$C/configs"
rm -rf "$CC"
mkdir -p "$CC/codex" "$CC/opencodex" "$CC/opencode" "$CC/opencode-data"

# codex user-level home: configs, auth, catalog, skills/plugins/rules; no
# runtime state (sessions, logs, sqlite DBs, packages, caches).
if [ -d "$HOME/.codex" ]; then
  rsync -a \
    --exclude 'sessions/' --exclude 'log/' --exclude 'tmp/' \
    --exclude 'shell_snapshots/' --exclude 'memories/' \
    --exclude 'packages/' --exclude 'cache/' \
    --exclude '*.sqlite*' --exclude 'history.jsonl' \
    --exclude 'session_index.jsonl' --exclude '*.bak*' \
    "$HOME/.codex/" "$CC/codex/"
fi

# opencodex: config.json (crucial) + auth/account state; no runtime state
# (logs, pid/port files, usage, sqlite mutation DB, backups, artifacts).
if [ -d "$HOME/.opencodex" ]; then
  rsync -a \
    --exclude '*.log' --exclude '*.pid' --exclude 'runtime-port.json' \
    --exclude 'service-state.json' --exclude 'usage.jsonl' \
    --exclude 'responses-state.json' --exclude 'config-mutation.sqlite*' \
    --exclude 'artifacts/' --exclude 'winsw/' --exclude '*.bak*' \
    --exclude 'catalog-backup*' \
    "$HOME/.opencodex/" "$CC/opencodex/"
fi

# opencode config: everything except node_modules/logs/backups (npm ci runs
# in the image, so deps are rebuilt there).
if [ -d "$HOME/.config/opencode" ]; then
  rsync -a \
    --exclude 'node_modules/' --exclude '*.log' --exclude '*.bak*' \
    "$HOME/.config/opencode/" "$CC/opencode/"
fi

# opencode auth store only — never the 33 GB session DB. Files land directly
# in $CC/opencode-data so the Dockerfile COPY puts auth.json at
# ~/.local/share/opencode/auth.json.
for f in auth.json account.json; do
  [ -f "$HOME/.local/share/opencode/$f" ] \
    && cp -a "$HOME/.local/share/opencode/$f" "$CC/opencode-data/"
done

echo "staged configs: $(du -sh "$CC" | cut -f1)  $CC"

echo "== docker build =="
docker build -f docker/Dockerfile -t "$IMAGE" .
echo "built $IMAGE"

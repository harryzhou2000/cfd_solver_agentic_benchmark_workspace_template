#!/usr/bin/env bash
# Stage host artifacts into docker/.context (hardlinks when possible), then
# build the image. Run from the repo root.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

IMAGE="${IMAGE:-cfd-bench:latest}"
C="$ROOT/docker/.context"
mkdir -p "$C"

# Proxy for the build itself (apt/npm/bun): source ~/.setproxy.sh when present
# and forward the standard proxy vars as docker build args.
PROXY_SCRIPT="${PROXY_SCRIPT:-$HOME/.setproxy.sh}"
if [ -f "$PROXY_SCRIPT" ]; then
  . "$PROXY_SCRIPT" || true
  echo "sourced proxy env from $PROXY_SCRIPT (used by docker build)"
fi
BUILD_ARGS=()
for v in HTTP_PROXY HTTPS_PROXY NO_PROXY http_proxy https_proxy no_proxy; do
  if [ -n "${!v:-}" ]; then BUILD_ARGS+=(--build-arg "$v=${!v}"); fi
done

echo "== checking submodules =="
for sm in opencodex ocx-relay OpenCode-goal-plugin; do
  case "$sm" in
    opencodex) marker="bin/ocx.mjs" ;;
    ocx-relay) marker="deepseek-relay.mjs" ;;
    *) marker="package.json" ;;
  esac
  if [ ! -e "$ROOT/$sm/$marker" ]; then
    echo "ERROR: submodule $sm is not initialized (missing $sm/$marker)." >&2
    echo "       Run: git submodule update --init --recursive" >&2
    exit 1
  fi
done
echo "  submodules OK"

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
OPENCODE_BIN="${OPENCODE_BIN:-$HOME/.opencode/bin/opencode}"
if [ ! -e "$OPENCODE_BIN" ] && command -v opencode >/dev/null 2>&1; then
  OPENCODE_BIN="$(command -v opencode)"
  echo "  (opencode not at default path; using $OPENCODE_BIN)"
fi
stage "$OPENCODE_BIN" "$C/opencode"

CODEX_RELEASE="${CODEX_RELEASE:-$(ls -d "$HOME"/.codex/packages/standalone/releases/*/ 2>/dev/null | sort -V | tail -1)}"
if [ -z "$CODEX_RELEASE" ] && command -v codex >/dev/null 2>&1; then
  echo "  (no standalone codex release under ~/.codex/packages; staging PATH codex binary)"
  mkdir -p "$C/codex-release/bin"
  cp -aL "$(command -v codex)" "$C/codex-release/bin/codex"
else
  stage "$CODEX_RELEASE" "$C/codex-release"
fi

CODEGRAPH_SRC="${CODEGRAPH_SRC:-$HOME/.codegraph/versions/v1.2.0}"
if [ ! -e "$CODEGRAPH_SRC" ] && command -v codegraph >/dev/null 2>&1; then
  echo "  (codegraph not at default path; staging PATH binary into bin/)"
  mkdir -p "$C/codegraph/bin"
  cp -aL "$(command -v codegraph)" "$C/codegraph/bin/codegraph"
else
  stage "$CODEGRAPH_SRC" "$C/codegraph"
fi

OPENMPI_PREFIX="${OPENMPI_PREFIX:-/mnt/ssd-SATARAID5/harry/tools/openmpi-5.0.9/install}"
stage "$OPENMPI_PREFIX" "$C/openmpi"
EXTERNAL_SRC="${EXTERNAL_SRC:-$ROOT/../opencode_omoslim_deepseek/external}"
stage "$EXTERNAL_SRC" "$C/external"
GOAL_PLUGIN_SRC="${GOAL_PLUGIN_SRC:-$HOME/.local/share/opencode-goal-plugin}"
stage "$GOAL_PLUGIN_SRC" "$C/opencode-goal-plugin"

# Placeholders so docker build never fails on a missing staged artifact (the
# image just ends up without that tool).
for t in codex-release codegraph openmpi external opencode-goal-plugin; do
  [ -e "$C/$t" ] || mkdir -p "$C/$t"
done
[ -e "$C/opencode" ] || : > "$C/opencode"

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
# --network host: build steps (apt/npm/bun) run in isolated build containers
# where 127.0.0.1 is NOT the host — required when the proxy sits on the
# host's loopback (and harmless for LAN proxies).
docker build --network host "${BUILD_ARGS[@]}" -f docker/Dockerfile -t "$IMAGE" .
echo "built $IMAGE"

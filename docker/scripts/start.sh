#!/usr/bin/env bash
# Interactive launcher for a benchmark contestant container.
#
# Usage:
#   docker/scripts/start.sh [--workspace DIR] [--image-config] [--harness shell|codex|opencode] [-- cmd...]
#
# Default mode mounts the live host config dirs (~/.codex, ~/.config/opencode,
# ~/.local/share/opencode, ~/.opencodex, ~/.codegraph) at the same absolute
# paths, so real credentials stay on the host and are never baked into the
# image. Codex and opencode sessions are bundled inside the workspace under
# .sessions/ (codex/ + opencode-data/), so a contestant run leaves persistent
# sessions in the working directory itself.
#
# The container runs with --rm plus an EXIT/INT/TERM/HUP trap that
# force-removes it, so no bench-* container survives the launcher (Ctrl-C,
# SIGTERM, or a closed terminal). docker run runs in the background and the
# launcher waits on it: bash only executes traps immediately while blocked in
# the wait builtin — a plain foreground `docker run` defers traps until the
# container exits on its own.
#
# --security-opt seccomp=unconfined,apparmor=unconfined: codex's bundled
# bubblewrap sandbox needs user namespaces and mount operations inside the
# container, which Docker's default seccomp/apparmor profiles block. The
# container is already the benchmark's isolation boundary; these flags only
# relax the container-internal layers. Without them codex degrades to
# "sandbox failed -> retry unsandboxed" on every tool call.
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

NAME="bench-$(basename "$WS" | tr -c 'A-Za-z0-9_.-' '_')"
MOUNTS=(-v "$BENCH_ROOT:$BENCH_ROOT")
ENVS=(-e HOME="$HOME")
SECURITY_OPTS=(--security-opt seccomp=unconfined --security-opt apparmor=unconfined)

if [ "$MOUNT_CONFIG" = "1" ]; then
  for d in .codex .config/opencode .local/share/opencode .opencodex .codegraph; do
    mkdir -p "$HOME/$d"
    MOUNTS+=(-v "$HOME/$d:$HOME/$d")
  done

  # --- persistent sessions bundled in the workspace ---------------------
  SESS="$WS/.sessions"
  CODEX_HOME_DIR="$SESS/codex"
  OC_DATA_DIR="$SESS/opencode-data/opencode"
  mkdir -p "$CODEX_HOME_DIR" "$OC_DATA_DIR"

  # codex: config/auth stay symlinked to the host, while sessions, logs and
  # DBs are written into $WS/.sessions/codex.
  for f in auth.json config.toml ocx.config.toml opencodex.config.toml \
           opencodex-catalog.json AGENTS.md cloud-config-bundle-cache.json \
           models_cache.json version.json; do
    [ -e "$HOME/.codex/$f" ] && ln -sfn "$HOME/.codex/$f" "$CODEX_HOME_DIR/$f"
  done
  # opencode: auth store symlinked; opencode.db/logs/storage land in the
  # workspace (the host DB is huge and shared with live opencode processes).
  [ -e "$HOME/.local/share/opencode/auth.json" ] \
    && ln -sfn "$HOME/.local/share/opencode/auth.json" "$OC_DATA_DIR/auth.json"
  [ -e "$HOME/.local/share/opencode/account.json" ] \
    && ln -sfn "$HOME/.local/share/opencode/account.json" "$OC_DATA_DIR/account.json"

  ENVS+=(-e CODEX_HOME="$CODEX_HOME_DIR" -e XDG_DATA_HOME="$SESS/opencode-data")

  # keep bundled sessions out of git (also covers pre-existing workspaces)
  if [ -d "$WS/.git" ]; then
    EXCLUDE="$WS/.git/info/exclude"
    grep -qxF '.sessions/' "$EXCLUDE" 2>/dev/null || printf '.sessions/\n' >> "$EXCLUDE"
  fi
else
  echo "using baked (redacted) configs instead of live host configs"
  echo "  (no real credentials; sessions are not persisted to the workspace)"
fi

echo "== container =="
echo "  image:     $IMAGE"
echo "  workspace: $WS"
echo "  harness:   $HARNESS"
if [ "$MOUNT_CONFIG" = "1" ]; then
  echo "  sessions:  $WS/.sessions/codex (codex), $WS/.sessions/opencode-data (opencode)"
fi
echo "  mounts:    ${MOUNTS[*]}"
echo

cleanup() {
  # --rm already removes the container on normal exit; this covers Ctrl-C /
  # kill / terminal close, and is a no-op once the container is gone.
  docker rm -f "$NAME" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM HUP

# Remove a container orphaned by a previous SIGKILLed session (SIGKILL cannot
# be trapped; the name is deterministic per workspace).
docker rm -f "$NAME" >/dev/null 2>&1 || true

docker run -it --rm --network host --name "$NAME" \
  --user "$(id -u):$(id -g)" \
  "${SECURITY_OPTS[@]}" \
  "${ENVS[@]}" \
  "${MOUNTS[@]}" \
  -w "$WS" \
  "$IMAGE" "${CMD[@]}" <&0 &
RUN_PID=$!
wait "$RUN_PID"

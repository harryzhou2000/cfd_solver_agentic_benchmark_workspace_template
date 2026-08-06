#!/usr/bin/env bash
# Interactive launcher for a benchmark contestant container.
#
# Usage:
#   docker/scripts/start.sh [--workspace DIR] [--image-config]
#     [--mount-host-configs] [--harness shell|codex|opencode]
#     [--codex-profile ocx] [-- cmd...]
#
# The image is self-contained: build.sh mirrors the live host user configs
# (~/.codex, ~/.opencodex, ~/.config/opencode, the opencode auth store) into
# the image (gitignored staging, real keys), so opencode and opencodex read
# their configs from the container home (~/.config/opencode, ~/.opencodex).
#
# Default mode additionally snapshots a per-workspace copy of the codex
# config set into $WS/.sessions/codex and mounts that dir at
# /home/harry/.codex: the effective codex home is the inside-docker-home path
# AND sessions/logs/DBs persist in the workspace. opencode data goes to
# $WS/.sessions/opencode-data (fresh DB + auth copy); the opencodex config is
# recorded at $WS/.sessions/opencodex/config.json.
#
# --image-config: use the baked configs directly, no workspace session bundle
# (sessions are ephemeral; for bare/CI runs).
# --mount-host-configs: additionally bind-mount the live host config dirs
# (~/.config/opencode, ~/.local/share/opencode, ~/.opencodex) over the baked
# ones, for live-edit workflows without an image rebuild.
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
MOUNT_HOST=0
HARNESS="shell"
CODEX_PROFILE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --workspace) WS="$2"; shift 2 ;;
    --image-config) MOUNT_CONFIG=0; shift ;;
    --mount-host-configs) MOUNT_HOST=1; shift ;;
    --harness) HARNESS="$2"; shift 2 ;;
    --codex-profile) CODEX_PROFILE="$2"; shift 2 ;;
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
if [ "$HARNESS" = "codex" ] && [ -n "$CODEX_PROFILE" ] && [ $# -eq 0 ]; then
  # Route codex through the user-level opencodex profile (e.g. -p ocx) so
  # benchmark sessions go through the proxy; codex project config cannot set
  # provider routing, so the profile is the mechanism.
  CMD=("codex" "-p" "$CODEX_PROFILE")
fi
if [ $# -gt 0 ]; then CMD=("$@"); fi

NAME="bench-$(basename "$WS" | tr -c 'A-Za-z0-9_.-' '_')"
MOUNTS=(-v "$BENCH_ROOT:$BENCH_ROOT")
# Mount the workspace itself so session paths work regardless of location
# (real workspaces live under $BENCH_ROOT, but relative/absolute paths from
# setup-workspace.sh may point anywhere).
MOUNTS+=(-v "$WS:$WS")
ENVS=(-e HOME="$HOME")
SECURITY_OPTS=(--security-opt seccomp=unconfined --security-opt apparmor=unconfined)

# codegraph index cache (host) stays mounted in every mode.
mkdir -p "$HOME/.codegraph"
MOUNTS+=(-v "$HOME/.codegraph:$HOME/.codegraph")

if [ "$MOUNT_CONFIG" = "1" ]; then
  SESS="$WS/.sessions"
  CODEX_HOME_DIR="$SESS/codex"
  OC_DATA_DIR="$SESS/opencode-data/opencode"
  mkdir -p "$CODEX_HOME_DIR" "$OC_DATA_DIR" "$SESS/opencodex"

  echo "== snapshotting user configs into the workspace (.sessions) =="
  # codex user-level config set as copies (not symlinks): this dir is mounted
  # at /home/harry/.codex inside the container, so it is both the per-workspace
  # record and the effective codex home. Sessions/state from earlier runs are
  # left untouched.
  if [ -d "$HOME/.codex" ]; then
    rsync -a \
      --exclude 'sessions/' --exclude 'log/' --exclude 'tmp/' \
      --exclude 'shell_snapshots/' --exclude 'memories/' \
      --exclude 'packages/' --exclude 'cache/' \
      --exclude '*.sqlite*' --exclude 'history.jsonl' \
      --exclude 'session_index.jsonl' --exclude '*.bak*' \
      "$HOME/.codex/" "$CODEX_HOME_DIR/"
  fi
  for f in auth.json account.json; do
    [ -f "$HOME/.local/share/opencode/$f" ] \
      && cp -a "$HOME/.local/share/opencode/$f" "$OC_DATA_DIR/"
  done
  [ -f "$HOME/.opencodex/config.json" ] \
    && cp -a "$HOME/.opencodex/config.json" "$SESS/opencodex/config.json"

  MOUNTS+=(-v "$CODEX_HOME_DIR:/home/harry/.codex")
  ENVS+=(-e CODEX_HOME=/home/harry/.codex -e XDG_DATA_HOME="$SESS/opencode-data")

  # keep bundled sessions out of git (also covers pre-existing workspaces)
  if [ -d "$WS/.git" ]; then
    EXCLUDE="$WS/.git/info/exclude"
    grep -qxF '.sessions/' "$EXCLUDE" 2>/dev/null || printf '.sessions/\n' >> "$EXCLUDE"
    grep -qxF '.opencode/' "$EXCLUDE" 2>/dev/null || printf '.opencode/\n' >> "$EXCLUDE"
  fi
else
  echo "using baked image configs directly (no workspace session bundle; sessions are ephemeral)"
fi

if [ "$MOUNT_HOST" = "1" ]; then
  echo "mounting live host config dirs over the baked ones"
  for d in .config/opencode .local/share/opencode .opencodex; do
    mkdir -p "$HOME/$d"
    MOUNTS+=(-v "$HOME/$d:$HOME/$d")
  done
fi

echo "== container =="
echo "  image:     $IMAGE"
echo "  workspace: $WS"
echo "  harness:   $HARNESS"
[ -n "$CODEX_PROFILE" ] && echo "  codex profile: -p $CODEX_PROFILE"
if [ "$MOUNT_CONFIG" = "1" ]; then
  echo "  codex home:  $WS/.sessions/codex -> /home/harry/.codex"
  echo "  opencode data: $WS/.sessions/opencode-data"
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

#!/usr/bin/env bash
# Must run under bash (arrays, ${!var}, ${var+x} ...). If this message ever
# appears instead of a run, the script was invoked with a non-bash shell:
#   bash docker/scripts/start.sh ...
if [ -z "${BASH_VERSION:-}" ]; then
  echo "start.sh must be run with bash, not ${0##*/}: bash docker/scripts/start.sh ..." >&2
  exit 2
fi

# Interactive launcher for a benchmark contestant container.
#
# Usage:
#   docker/scripts/start.sh [--workspace DIR] [--host-credentials]
#     [--image-config] [--mount-host-configs]
#     [--harness shell|codex|opencode] [--codex-profile ocx]
#     [--name NAME] [--cpus N] [-- cmd...]
#   CONFIG_STACK=/path/to/stack CPUS=8 OCX_PORT=10109 \
#     docker/scripts/start.sh --workspace DIR
#
# The image is built from fresh official installs only (see docker/Dockerfile
# + docker/opencode-plugins.json) — user configs are NEVER baked into it.
# This launcher installs the VENDORED config stack at container start
# (default: <repo>/docker/configs, gathered from the current host with
# sync-configs.sh and committed credential-free; override with CONFIG_STACK):
#   - codex      -> $WS/.sessions/codex            mounted at /home/cfd_agent/.codex
#   - opencode   -> $WS/.sessions/opencode-config  mounted at /home/cfd_agent/.config/opencode
#   - opencodex  -> $WS/.sessions/opencodex        mounted at /home/cfd_agent/.opencodex
#   - bash       -> $WS/.sessions/bash             mounted at ~/.bashrc/.profile/... (default Ubuntu setup)
# Sessions/logs/DBs persist in $WS/.sessions for all three harnesses.
# The stack's apiKeys are env references: export the vars on this host, or
# pass --host-credentials to export the real keys from the live host configs
# (read-only; never written to the workspace). The live host config stack is
# never used as the config source.
#
# User mapping (enroot-style): the image ships one generic user (cfd_agent).
# The entrypoint remaps it to this host's uid/gid at start (HOST_UID/HOST_GID
# env), so files created in the container are owned by the invoking user and
# the mounted host dirs work unchanged. The container is entered as root and
# the entrypoint drops privileges before exec'ing the command.
#
# --host-credentials: read real apiKeys/auth from THIS host's live configs
# and pass them to the container as env vars / auth-file binds (read-only;
# the config stack itself is still the vendored one).
# --image-config: use the image's pristine state directly (no config stack,
# no workspace session bundle; sessions are ephemeral; for bare/CI runs).
# --mount-host-configs: bind-mount the live host config dirs
# (~/.codex, ~/.config/opencode, ~/.local/share/opencode, ~/.opencodex) over
# the installed stack, for live-edit workflows without a rebuild
# (non-reproducible escape hatch).
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
IMG_HOME="/home/cfd_agent"
WS=""
MOUNT_CONFIG=1
MOUNT_HOST=0
HOST_CRED=0
HARNESS="shell"
CODEX_PROFILE=""
NAME_ARG=""
CPUS="${CPUS:-4}"   # --cpus quota: how many CPU cores worth of time the
                     # container may use (docker's --cpus, not cpuset pins)

while [ $# -gt 0 ]; do
  case "$1" in
    --workspace) WS="$2"; shift 2 ;;
    --image-config) MOUNT_CONFIG=0; shift ;;
    --mount-host-configs) MOUNT_HOST=1; shift ;;
    --host-credentials) HOST_CRED=1; shift ;;
    --harness) HARNESS="$2"; shift 2 ;;
    --codex-profile) CODEX_PROFILE="$2"; shift 2 ;;
    --name|-n) NAME_ARG="$2"; shift 2 ;;
    --cpus) CPUS="$2"; shift 2 ;;
    --) shift; break ;;
    *) WS="${WS:-$1}"; shift ;;
  esac
done

case "$CPUS" in
  ''|*[!0-9.]*) echo "invalid --cpus '$CPUS' (expected a number, e.g. 4)" >&2; exit 1 ;;
esac

WS="$(realpath "${WS:-$PWD}")"
if [ ! -d "$WS" ]; then
  echo "workspace $WS does not exist" >&2
  exit 1
fi

case "$HARNESS" in
  shell) CMD=("/bin/bash") ;;
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

NAME="${NAME_ARG:-bench-$(basename "$WS" | tr -c 'A-Za-z0-9_.-' '_')}"
MOUNTS=(-v "$BENCH_ROOT:$BENCH_ROOT")
# Mount the workspace itself so session paths work regardless of location
# (real workspaces live under $BENCH_ROOT, but relative/absolute paths from
# setup-workspace.sh may point anywhere).
MOUNTS+=(-v "$WS:$WS")
# WORKSPACE tells the entrypoint to start the shell in the contestant
# workspace (also set as the docker working dir via -w below).
ENVS=(-e HOME="$IMG_HOME" -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
      -e WORKSPACE="$WS")
SECURITY_OPTS=(--security-opt seccomp=unconfined --security-opt apparmor=unconfined)

# Proxy: forward the existing proxy env into the container. With --network
# host the proxy endpoints (LAN IPs, loopback) are reachable from inside the
# container; loopback stays out of NO_PROXY so the opencodex proxy on
# 127.0.0.1:10109 is never proxied.
for host in localhost 127.0.0.1 ::1; do
  case ",${NO_PROXY:-}," in *",$host,"*) ;; *) NO_PROXY="${NO_PROXY:+$NO_PROXY,}$host" ;; esac
done
for v in HTTP_PROXY HTTPS_PROXY ALL_PROXY NO_PROXY http_proxy https_proxy all_proxy no_proxy; do
  if [ -n "${!v:-}" ]; then ENVS+=(-e "$v=${!v}"); fi
done

# codegraph index cache (host) stays mounted in every mode.
mkdir -p "$HOME/.codegraph"
MOUNTS+=(-v "$HOME/.codegraph:$HOME/.codegraph")

if [ "$MOUNT_CONFIG" = "1" ]; then
  # The config stack installed at start: the repo's vendored configs by
  # default (gathered from the current host with sync-configs.sh, redacted /
  # env-referenced, committed); override with CONFIG_STACK=/path/to/stack.
  CONFIG_STACK="${CONFIG_STACK:-$ROOT/docker/configs}"
  if [ ! -f "$CONFIG_STACK/opencode/opencode.jsonc" ] \
     || [ ! -d "$CONFIG_STACK/codex" ] \
     || [ ! -d "$CONFIG_STACK/opencodex" ]; then
    echo "ERROR: config stack incomplete at $CONFIG_STACK" >&2
    echo "       regenerate it with: docker/scripts/sync-configs.sh --env-mode" >&2
    exit 1
  fi

  SESS="$WS/.sessions"
  CODEX_HOME_DIR="$SESS/codex"
  OC_CONFIG_DIR="$SESS/opencode-config"
  OC_DATA_DIR="$SESS/opencode-data/opencode"
  OCX_DIR="$SESS/opencodex"

  echo "== installing vendored config stack ($CONFIG_STACK) into $SESS =="
  echo "  (credential-free: apiKeys are env references; supply them via"
  echo "   exported env vars or --host-credentials)"

  # codex: copy of the vendored codex config set, mounted as the effective
  # codex home; sessions/logs/DBs persist in the workspace copy.
  mkdir -p "$CODEX_HOME_DIR"
  rsync -a --no-owner --no-group "$CONFIG_STACK/codex/" "$CODEX_HOME_DIR/"
  MOUNTS+=(-v "$CODEX_HOME_DIR:$IMG_HOME/.codex")
  ENVS+=(-e CODEX_HOME="$IMG_HOME/.codex")

  # opencode: vendored config dir mounted at the global config path; data
  # dir (sessions, DB, auth) is bundled fresh in the workspace.
  mkdir -p "$OC_CONFIG_DIR" "$OC_DATA_DIR"
  rsync -a --no-owner --no-group "$CONFIG_STACK/opencode/" "$OC_CONFIG_DIR/"
  MOUNTS+=(-v "$OC_CONFIG_DIR:$IMG_HOME/.config/opencode")
  ENVS+=(-e XDG_CONFIG_HOME="$IMG_HOME/.config" -e XDG_DATA_HOME="$SESS/opencode-data")

  # opencodex: vendored config dir mounted at the service's home path;
  # runtime state (usage, artifacts, sqlite) persists in the workspace.
  mkdir -p "$OCX_DIR"
  rsync -a --no-owner --no-group "$CONFIG_STACK/opencodex/" "$OCX_DIR/"
  MOUNTS+=(-v "$OCX_DIR:$IMG_HOME/.opencodex")

  # bash: default Ubuntu bash setup (system template vendored under
  # docker/configs/bash — the manager host's bashrc settings + friendly
  # .inputrc; override with CONFIG_STACK/bash. Seeded into the session
  # bundle at start, then mounted at the home dotfiles so every workspace
  # gets a writable, persistent ~/.bashrc, readline config and history.
  BASH_DIR="$SESS/bash"
  BASH_TEMPLATE="$CONFIG_STACK/bash"
  [ -d "$BASH_TEMPLATE" ] || BASH_TEMPLATE="$ROOT/docker/configs/bash"
  mkdir -p "$BASH_DIR"
  for f in .bashrc .bash_profile .profile .bash_logout .inputrc .alias .envset; do
    if [ ! -f "$BASH_DIR/$f" ] && [ -f "$BASH_TEMPLATE/$f" ]; then
      cp "$BASH_TEMPLATE/$f" "$BASH_DIR/$f"
    fi
  done
  touch "$BASH_DIR/.bash_history"
  for f in .bashrc .bash_profile .profile .bash_logout .inputrc .alias .envset .bash_history; do
    MOUNTS+=(-v "$BASH_DIR/$f:$IMG_HOME/$f")
  done

  # Forward credential env vars already exported on this host (the stack's
  # env references). Nothing is read from host config files unless
  # --host-credentials is given.
  for v in $(env | sed -n 's/^\(OPENCODE_[A-Z0-9_]*\)=.*/\1/p'); do
    ENVS+=(-e "$v")
  done
  for v in $(env | sed -n 's/^\(OPENCODEX_[A-Z0-9_]*\)=.*/\1/p'); do
    ENVS+=(-e "$v")
  done

  if [ "$HOST_CRED" = "1" ]; then
    echo "== --host-credentials: exporting real apiKeys from this host's live configs into the container env (read-only; never written to the workspace) =="
    if [ -f "$HOME/.config/opencode/opencode.jsonc" ]; then
      while IFS= read -r kv; do
        [ -n "$kv" ] && ENVS+=(-e "$kv")
      done < <(python3 - "$HOME/.config/opencode/opencode.jsonc" <<'PYEOF'
import re, sys
from pathlib import Path

KEY = re.compile(r"^(\s*\")([A-Za-z0-9_.-]+)(\"\s*:\s*\{)")
APIKEY = re.compile(r"^(\s*\"apiKey\"\s*:\s*\")([^\"]+)")

def stem(name):
    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()

depth = 0
in_providers = False
provider_depth = 0
provider = None
for line in Path(sys.argv[1]).read_text().splitlines():
    stripped = line.lstrip()
    if stripped.startswith("//"):
        continue
    m = KEY.match(line)
    if m:
        name = m.group(2)
        if in_providers and depth == provider_depth:
            provider = name
        if not in_providers and depth == 1 and name in ("provider", "providers"):
            in_providers = True
            provider_depth = depth + line.count("{") - line.count("}")
        depth += line.count("{") - line.count("}")
        if in_providers and depth < provider_depth:
            in_providers = False
            provider = None
        continue
    m = APIKEY.match(line)
    if m and provider:
        key = m.group(2)
        if key.startswith("sk-"):
            print(f"OPENCODE_API_KEY_{stem(provider)}={key}")
    depth += line.count("{") - line.count("}")
    if in_providers and depth < provider_depth:
        in_providers = False
        provider = None
PYEOF
)
    fi
    if [ -f "$HOME/.opencodex/config.json" ]; then
      while IFS= read -r kv; do
        [ -n "$kv" ] && ENVS+=(-e "$kv")
      done < <(python3 - "$HOME/.opencodex/config.json" <<'PYEOF'
import json, re, sys

cfg = json.load(open(sys.argv[1]))

def stem(name):
    return re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_").upper()

for pid, prov in (cfg.get("providers") or {}).items():
    if not isinstance(prov, dict):
        continue
    name = f"OPENCODEX_{stem(pid)}_API_KEY"
    if isinstance(prov.get("apiKey"), str) and prov["apiKey"].startswith("sk-"):
        print(f"{name}={prov['apiKey']}")
    elif isinstance(prov.get("apiKeyPool"), list):
        for entry in prov["apiKeyPool"]:
            if isinstance(entry, dict) and isinstance(entry.get("key"), str) \
               and entry["key"].startswith("sk-"):
                print(f"{name}={entry['key']}")
                break
PYEOF
)
    fi
    if [ -f "$HOME/.codex/auth.json" ]; then
      touch "$CODEX_HOME_DIR/auth.json"   # non-credential placeholder; real file mounts over it
      MOUNTS+=(-v "$HOME/.codex/auth.json:$IMG_HOME/.codex/auth.json")
    fi
    for f in auth.json account.json; do
      if [ -f "$HOME/.local/share/opencode/$f" ]; then
        touch "$OC_DATA_DIR/$f"           # non-credential placeholder; real file mounts over it
        MOUNTS+=(-v "$HOME/.local/share/opencode/$f:$OC_DATA_DIR/$f")
      fi
    done
  fi

  # keep bundled sessions out of git (also covers pre-existing workspaces)
  if [ -d "$WS/.git" ]; then
    EXCLUDE="$WS/.git/info/exclude"
    grep -qxF '.sessions/' "$EXCLUDE" 2>/dev/null || printf '.sessions/\n' >> "$EXCLUDE"
    grep -qxF '.opencode/' "$EXCLUDE" 2>/dev/null || printf '.opencode/\n' >> "$EXCLUDE"
  fi
else
  echo "using the image's pristine state directly (no config stack, no workspace session bundle; sessions are ephemeral)"
fi

if [ "$MOUNT_HOST" = "1" ]; then
  echo "WARNING: --mount-host-configs mounts the live host config dirs over the vendored stack (non-reproducible escape hatch)"
  for d in .codex .config/opencode .local/share/opencode .opencodex; do
    if [ -d "$HOME/$d" ]; then
      MOUNTS+=(-v "$HOME/$d:$IMG_HOME/$d")
    else
      echo "  (skip: $HOME/$d does not exist on this host)"
    fi
  done
fi

# Announce every env var passed to the container. Values never print fully:
# long values are masked to head+tail so credential keys stay usable for
# identification without leaking (--host-credentials mode exports real keys
# into these vars).
mask_val() {
  local v="$1" n="${#1}"
  if [ "$n" -le 16 ]; then
    printf '%s' "$v"
  else
    printf '%s…%s' "${v:0:6}" "${v: -4}"
  fi
}
echo "== environment ($((${#ENVS[@]} / 2)) vars) =="
# ENVS alternates `-e` markers and NAME[=value] entries (docker -e pairs).
for e in "${ENVS[@]}"; do
  case "$e" in
    -e) continue ;;   # marker element; the actual entry follows
    *=*)
      name="${e%%=*}"; val="${e#*=}"
      printf '  %-44s %s\n' "$name" "$(mask_val "$val")" ;;
    *)
      name="$e"
      if [ -n "${!name+x}" ]; then
        printf '  %-44s %s (inherited)\n' "$name" "$(mask_val "${!name}")"
      else
        printf '  %-44s (unset)\n' "$name"
      fi ;;
  esac
done

echo "== container =="
echo "  image:     $IMAGE"
echo "  workspace: $WS"
echo "  harness:   $HARNESS"
echo "  name:      $NAME"
echo "  cpus:      $CPUS"
echo "  start dir: $WS (entrypoint cd + docker -w)"
[ -n "$CODEX_PROFILE" ] && echo "  codex profile: -p $CODEX_PROFILE"
if [ "$MOUNT_CONFIG" = "1" ]; then
  echo "  config stack: $CONFIG_STACK"
  echo "  bash:         $WS/.sessions/bash (default Ubuntu .bashrc/.profile + persistent history)"
  echo "  codex:        $WS/.sessions/codex -> $IMG_HOME/.codex"
  echo "  opencode:     $WS/.sessions/opencode-config -> $IMG_HOME/.config/opencode"
  echo "  opencode data:$WS/.sessions/opencode-data (XDG_DATA_HOME)"
  echo "  opencodex:    $WS/.sessions/opencodex -> $IMG_HOME/.opencodex"
  [ "$HOST_CRED" = "1" ] && echo "  credentials:  host-credentials mode (live keys via env/binds)"
  [ "$MOUNT_HOST" = "1" ] && echo "  credentials/configs: live host dirs mounted over the stack"
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
  --user root:root \
  --cpus "$CPUS" \
  -w "$WS" \
  "${SECURITY_OPTS[@]}" \
  "${ENVS[@]}" \
  "${MOUNTS[@]}" \
  "$IMAGE" "${CMD[@]}" <&0 &
RUN_PID=$!
wait "$RUN_PID"

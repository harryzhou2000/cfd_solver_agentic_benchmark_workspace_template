#!/usr/bin/env bash
# Container entrypoint: (1) enroot-style user mapping — remap the generic
# image user (cfd_agent) to the invoking host user's uid/gid so mounted host
# dirs (workspace, configs, .codegraph) are owned by the process; (2)
# optionally start the opencodex proxy; (3) exec the requested command
# (default: interactive shell).
#
# The image is user-agnostic: pass HOST_UID/HOST_GID to match any host user.
# Without them (bare runs) the command runs as root.
#
# Probe port resolution, in order of precedence:
#   1. $OCX_PORT env override (also passed to `ocx start --port`)
#   2. `.port` from ~/.opencodex/config.json
#   3. 10100 (the opencodex default)
# If something is already listening on that port (e.g. a host-side opencodex
# daemon when running with --network host), the container skips starting its
# own proxy.
set -e

IMG_USER="cfd_agent"

# Seed the default Ubuntu bash setup (system template) into the image home
# when absent — bare `docker run` and --image-config mode get a normal bash
# environment (no zsh-newuser-style wizard, no missing dotfiles). start.sh
# bind-mounts a per-workspace copy over these, so interactive containers use
# the persistent version.
if [ -d /etc/skel ]; then
  for f in /etc/skel/.[!.]*; do
    [ -f "$f" ] || continue
    base="$(basename "$f")"
    [ -e "/home/$IMG_USER/$base" ] || cp "$f" "/home/$IMG_USER/$base"
  done
fi

if [ "$(id -u)" = "0" ] && [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
  if [ "$HOST_UID" != "$(id -u "$IMG_USER")" ] || [ "$HOST_GID" != "$(id -g "$IMG_USER")" ]; then
    echo "[entrypoint] mapping $IMG_USER -> uid=$HOST_UID gid=$HOST_GID"
    # Direct passwd/group rewrite instead of usermod/groupmod: `usermod -u`
    # recursively re-owns the user's home directory, which on overlayfs
    # copy-up every file and stalls container start for minutes. The image
    # home is deliberately kept empty (see Dockerfile), and setpriv only
    # needs the passwd/group entries to resolve uid/gid + initgroups.
    sed -i "s/^\(${IMG_USER}:x:\)[0-9]*:[0-9]*:/\1${HOST_UID}:${HOST_GID}:/" /etc/passwd
    sed -i "s/^\(${IMG_USER}:x:\)[0-9]*:/\1${HOST_GID}:/" /etc/group
    if [ "$(id -u "$IMG_USER")" != "$HOST_UID" ] \
       || [ "$(id -g "$IMG_USER")" != "$HOST_GID" ]; then
      echo "[entrypoint] ERROR: failed to remap $IMG_USER to $HOST_UID:$HOST_GID" >&2
      exit 1
    fi
    # Shallow chown only: the image home content is baked world-writable
    # / (almost) empty, so only the home root + top-level entries need to
    # follow the new uid; a recursive chown would copy up the home on
    # overlayfs every start.
    chown "$HOST_UID:$HOST_GID" "/home/$IMG_USER"
    find "/home/$IMG_USER" -mindepth 1 -maxdepth 1 \
      -exec chown "$HOST_UID:$HOST_GID" {} +
  fi
  exec setpriv --reuid "$HOST_UID" --regid "$HOST_GID" --init-groups "$0" "$@"
fi

if [[ "${OPENCODEX_AUTOSTART:-1}" == "1" && -f "$HOME/.opencodex/config.json" ]]; then
  OCX_PORT="${OCX_PORT:-}"
  OCX_PORT_OVERRIDE=0
  if [ -z "$OCX_PORT" ]; then
    OCX_PORT="$(jq -r '.port // 10100' "$HOME/.opencodex/config.json" 2>/dev/null || true)"
    OCX_PORT="${OCX_PORT:-10100}"
  else
    OCX_PORT_OVERRIDE=1
  fi

  if ! (exec 3<>"/dev/tcp/127.0.0.1/$OCX_PORT") 2>/dev/null; then
    echo "[entrypoint] starting opencodex (ocx start) on 127.0.0.1:$OCX_PORT ..."
    START_ARGS=()
    [ "$OCX_PORT_OVERRIDE" = "1" ] && START_ARGS=(--port "$OCX_PORT")
    ( cd "$HOME/.opencodex" && nohup ocx start "${START_ARGS[@]}" \
        > "$HOME/.opencodex/container-opencodex.log" 2>&1 & )
    for _ in $(seq 1 15); do
      if (exec 3<>"/dev/tcp/127.0.0.1/$OCX_PORT") 2>/dev/null; then
        echo "[entrypoint] opencodex is up on $OCX_PORT"
        break
      fi
      sleep 1
    done
  else
    echo "[entrypoint] opencodex already listening on $OCX_PORT (host-side service?)"
  fi
fi

exec "$@"

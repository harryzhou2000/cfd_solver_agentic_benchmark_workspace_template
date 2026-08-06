#!/usr/bin/env bash
# Container entrypoint: optionally start the opencodex proxy, then exec the
# requested command (default: interactive shell).
#
# Probe port resolution, in order of precedence:
#   1. $OCX_PORT env override (also passed to `ocx start --port`)
#   2. `.port` from ~/.opencodex/config.json
#   3. 10100 (the opencodex default)
# If something is already listening on that port (e.g. a host-side opencodex
# daemon when running with --network host), the container skips starting its
# own proxy.
set -e

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

#!/usr/bin/env bash
# Container entrypoint: optionally start the opencodex proxy, then exec the
# requested command (default: interactive shell).
set -e

if [[ "${OPENCODEX_AUTOSTART:-1}" == "1" && -f "$HOME/.opencodex/config.json" ]]; then
  if ! (exec 3<>"/dev/tcp/127.0.0.1/10109") 2>/dev/null; then
    echo "[entrypoint] starting opencodex (ocx start) on 127.0.0.1:10109 ..."
    ( cd "$HOME/.opencodex" && nohup ocx start \
        > "$HOME/.opencodex/container-opencodex.log" 2>&1 & )
    for _ in $(seq 1 15); do
      if (exec 3<>"/dev/tcp/127.0.0.1/10109") 2>/dev/null; then
        echo "[entrypoint] opencodex is up on 10109"
        break
      fi
      sleep 1
    done
  else
    echo "[entrypoint] opencodex already listening on 10109 (host-side service?)"
  fi
fi

exec "$@"

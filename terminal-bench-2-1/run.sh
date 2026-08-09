#!/usr/bin/env bash
# Run Terminal-Bench 2.1 from a vendored job config.
#
# Usage:
#   ./terminal-bench-2-1/run.sh [config] [extra harbor flags...]
#
# Examples:
#   ./terminal-bench-2-1/run.sh                                     # default config
#   ./terminal-bench-2-1/run.sh terminal-bench-2-1/configs/deepseek-v4-flash-max.yaml
#   ./terminal-bench-2-1/run.sh terminal-bench-2-1/configs/deepseek-v4-flash-max.yaml --job-name run2
#   ./terminal-bench-2-1/run.sh -l 5 --print-config                  # flags use default config
#   ./terminal-bench-2-1/run.sh terminal-bench-2-1/job.example.yaml -l 5 --print-config
#
# The config path (including the default) is resolved relative to the current
# working directory — the same base harbor uses for the YAML's internal
# relative paths (jobs_dir, download_dir), so launch from the repo root.
# From inside terminal-bench-2-1/ use ./run.sh configs/deepseek-v4-flash-max.yaml.
#
# If the config contains "${LITELLM_API_KEY}", run.sh renders a throwaway
# copy under terminal-bench-2-1/.cache/rendered-configs/ with the value from
# the project-root .env, so vendored configs stay key-free.
#
# Extra CLI flags merge over the YAML (kwargs via --ak, retries via -r, task
# filters via -i/-l, timeouts, job dir via -o/--job-name). Note: -m is only
# honored together with -a, which rebuilds the agent and drops the YAML
# endpoint kwargs — to run another model, vendor another YAML instead.

set -euo pipefail

if [ $# -eq 0 ] || [[ "$1" == -* ]]; then
  CFG="terminal-bench-2-1/configs/deepseek-v4-flash-max.yaml"
else
  CFG="$1"
  shift
fi

if [ ! -f "$CFG" ]; then
  echo "config not found (resolved relative to cwd $PWD): $CFG" >&2
  echo "vendored configs:" >&2
  ls ./terminal-bench-2-1/configs/*.yaml ./configs/*.yaml ./terminal-bench-2-1/*.yaml 2>/dev/null >&2 || true
  exit 1
fi

ENV_FILE=""
if [ -f "./.env" ]; then
  ENV_FILE="./.env"
elif [ -f "../.env" ]; then
  ENV_FILE="../.env"
fi

# Render ${LITELLM_API_KEY} from .env into a throwaway config when referenced.
if grep -qF '${LITELLM_API_KEY}' "$CFG"; then
  if [ -z "$ENV_FILE" ]; then
    echo "config references \${LITELLM_API_KEY} but no .env was found (looked in ./ and ../)" >&2
    exit 1
  fi
  LITELLM_KEY="$(sed -n 's/^LITELLM_API_KEY=//p' "$ENV_FILE" | tail -1)"
  if [ -z "$LITELLM_KEY" ]; then
    echo "LITELLM_API_KEY is missing from $ENV_FILE" >&2
    exit 1
  fi
  RENDER_DIR="terminal-bench-2-1/.cache/rendered-configs"
  mkdir -p "$RENDER_DIR"
  RENDERED_CFG="$RENDER_DIR/$(basename "$CFG").rendered.yaml"
  LITELLM_KEY_SED="$(printf '%s' "$LITELLM_KEY" | sed 's/[&\\/]/\\&/g')"
  sed "s|\${LITELLM_API_KEY}|$LITELLM_KEY_SED|g" "$CFG" > "$RENDERED_CFG"
  echo "rendered $CFG -> $RENDERED_CFG (LITELLM_API_KEY injected from $ENV_FILE)" >&2
  CFG="$RENDERED_CFG"
fi

# This shell exports ALL_PROXY=socks5://... which harbor's httpx client cannot
# use (socksio is not installed in the uv tool venv). HTTP(S)_PROXY is kept —
# harbor reaches GitHub/registry through it fine.
if [ -n "$ENV_FILE" ]; then
  exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y --env-file "$ENV_FILE" "$@"
else
  exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y "$@"
fi

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

# This shell exports ALL_PROXY=socks5://... which harbor's httpx client cannot
# use (socksio is not installed in the uv tool venv). HTTP(S)_PROXY is kept —
# harbor reaches GitHub/registry through it fine.
if [ -n "$ENV_FILE" ]; then
  exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y --env-file "$ENV_FILE" "$@"
else
  exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y "$@"
fi

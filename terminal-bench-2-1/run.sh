#!/usr/bin/env bash
# Run Terminal-Bench 2.1 from a vendored job config.
#
# Usage:
#   ./terminal-bench-2-1/run.sh [config] [extra harbor flags...]
#
# Examples:
#   ./terminal-bench-2-1/run.sh                                    # default config
#   ./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml
#   ./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml --job-name run2
#   ./terminal-bench-2-1/run.sh -l 5 --print-config                 # flags use default config
#   ./terminal-bench-2-1/run.sh job.example.yaml -l 5 --print-config   # dry-run
#
# Extra CLI flags merge over the YAML (kwargs via --ak, retries via -r, task
# filters via -i/-l, timeouts, job dir via -o/--job-name). Note: -m is only
# honored together with -a, which rebuilds the agent and drops the YAML
# endpoint kwargs — to run another model, vendor another YAML instead.

set -euo pipefail

DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ $# -eq 0 ] || [[ "$1" == -* ]]; then
  CFG="configs/deepseek-v4-flash-max.yaml"
else
  CFG="$1"
  shift
fi

case "$CFG" in
  /*) ;;
  *) CFG="$DIR/$CFG" ;;
esac

if [ ! -f "$CFG" ]; then
  echo "config not found: $CFG" >&2
  echo "vendored configs:" >&2
  ls "$DIR"/configs/*.yaml "$DIR"/*.yaml 2>/dev/null >&2
  exit 1
fi

# This shell exports ALL_PROXY=socks5://... which harbor's httpx client cannot
# use (socksio is not installed in the uv tool venv). HTTP(S)_PROXY is kept —
# harbor reaches GitHub/registry through it fine.
exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y "$@"

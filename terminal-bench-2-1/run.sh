#!/usr/bin/env bash
# Run Terminal-Bench 2.1 with the terminus-2 harness, routing model calls
# through the local opencodex proxy (127.0.0.1:10109, loopback = no auth).
#
# Usage:
#   ./terminal-bench-2-1/run.sh [model-id] [n-tasks] [extra harbor flags...]
#
# Examples:
#   ./terminal-bench-2-1/run.sh                              # GLM-5.2, 5 tasks
#   ./terminal-bench-2-1/run.sh openai/deepseek/deepseek-v4-flash 10
#   ./terminal-bench-2-1/run.sh openai/BLSC/GLM-5.2 5 -i 'example*' -n 2

set -euo pipefail

MODEL="${1:-openai/BLSC/GLM-5.2}"
N_TASKS="${2:-5}"
shift 2 2>/dev/null || shift 1 2>/dev/null || true

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# This shell exports ALL_PROXY=socks5://... which harbor's httpx client cannot
# use (socksio is not installed in the uv tool venv). HTTP(S)_PROXY is kept —
# harbor reaches GitHub/registry through it fine.
exec env -u ALL_PROXY -u all_proxy harbor run \
  -d terminal-bench/terminal-bench-2-1 \
  -a terminus-2 \
  -m "$MODEL" \
  --ak api_base=http://127.0.0.1:10109/v1 \
  --ak 'llm_kwargs={"api_key":"ocx-loopback"}' \
  -l "$N_TASKS" \
  -n 4 -k 1 \
  -o "$DIR/jobs" \
  -y \
  "$@"

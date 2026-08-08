#!/usr/bin/env bash
# Run Terminal-Bench 2.1 with the terminus-2 harness, routing model calls
# through the local opencodex proxy (127.0.0.1:10109, loopback = no auth).
#
# This script generates a job YAML (model + task count baked in) and runs
# `harbor run -c <yaml>`. Anything else you pass is forwarded as CLI flags,
# which harbor merges over the YAML (e.g. reasoning effort, retries, task
# filters, timeout multipliers).
#
# Usage:
#   ./terminal-bench-2-1/run.sh [model-id] [n-tasks] [extra harbor flags...]
#
# Examples:
#   ./terminal-bench-2-1/run.sh                                     # GLM-5.2, 5 tasks
#   ./terminal-bench-2-1/run.sh openai/deepseek/deepseek-v4-pro 10
#   ./terminal-bench-2-1/run.sh openai/BLSC/GLM-5.2 5 --ak reasoning_effort=high
#   ./terminal-bench-2-1/run.sh openai/BLSC/GLM-5.2 5 -r 2 --agent-timeout-multiplier 1.5
#   ./terminal-bench-2-1/run.sh openai/BLSC/GLM-5.2 5 -i 'terminal-bench/example*'
#   ./terminal-bench-2-1/run.sh openai/BLSC/GLM-5.2 3 --print-config  # dry-run check

set -euo pipefail

MODEL="${1:-openai/BLSC/GLM-5.2}"
N_TASKS="${2:-5}"
shift 2 2>/dev/null || shift 1 2>/dev/null || true

DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_DIR="$DIR/.cache"
mkdir -p "$CACHE_DIR"

SAFE_MODEL="$(printf '%s' "$MODEL" | tr '/:' '__')"
CFG="$CACHE_DIR/job-$SAFE_MODEL-$N_TASKS.yaml"

cat > "$CFG" <<EOF
job_name: tb21-terminus2-${SAFE_MODEL}
jobs_dir: ${DIR}/jobs
n_attempts: 1
n_concurrent_trials: 4

agents:
  - name: terminus-2
    model_name: "${MODEL}"
    kwargs:
      api_base: http://127.0.0.1:10109/v1
      # litellm requires a non-empty api_key for the openai/ provider even on a
      # loopback bind; opencodex does not validate it there.
      llm_kwargs:
        api_key: ocx-loopback

datasets:
  - name: terminal-bench/terminal-bench-2-1
    download_dir: ${DIR}/.cache/tasks
    n_tasks: ${N_TASKS}
EOF

echo "Job config: ${CFG}"

# This shell exports ALL_PROXY=socks5://... which harbor's httpx client cannot
# use (socksio is not installed in the uv tool venv). HTTP(S)_PROXY is kept —
# harbor reaches GitHub/registry through it fine.
exec env -u ALL_PROXY -u all_proxy harbor run -c "$CFG" -y "$@"

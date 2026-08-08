# Terminal-Bench 2.1 via opencodex

Runs the official [Terminal-Bench 2.1](https://www.tbench.ai/docs/run-terminal-bench-2-1)
dataset with Harbor, using the **terminus-2** harness agent and routing every
model call through the local **opencodex** proxy (deepseek + BLSC providers).

## Why this works without extra plumbing

terminus-2 is a harbor-native agent: harbor drives a tmux session *inside* the
task container, but the LLM API calls are made by the harbor process on the
host. opencodex listens on `127.0.0.1:10109` and, on a loopback bind, requires
no API key — it only needs the model name. So the "model endpoint" for
terminus-2 is just:

```text
base_url: http://127.0.0.1:10109/v1      (chat/completions surface)
model:    openai/<ocx-model-id>          (openai/ prefix tells litellm to
                                          send the rest verbatim as the wire model)
```

CLI agents (codex, opencode, ...) are different: harbor installs and runs them
*inside* the task container, so they cannot see the host's loopback proxy.
That is a future point — see "CLI harnesses later" below.

## Prerequisites

- Harbor installed (`uv tool install harbor` → `~/.local/bin/harbor`), v0.20+.
- Docker daemon running.
- opencodex proxy running with the deepseek and BLSC providers:

  ```bash
  ocx status    # Proxy: running, health http://127.0.0.1:10109/healthz
  ocx models    # deepseek + BLSC model lists
  ```

## 1. Smoke test (dataset + containers, no model cost)

Run the oracle (precomputed) solutions on the first five tasks. This downloads
the dataset from the Harbor registry and builds/start the task containers:

```bash
harbor run -d terminal-bench/terminal-bench-2-1 -a oracle -l 5 \
  -o terminal-bench-2-1/jobs -y
```

Note: this shell exports `ALL_PROXY=socks5://…`, which harbor's httpx client
cannot use (`socksio` is missing from the uv tool venv). Strip it for harbor:

```bash
env -u ALL_PROXY -u all_proxy harbor run -d terminal-bench/terminal-bench-2-1 \
  -a oracle -l 5 -o terminal-bench-2-1/jobs -y
```

The HTTP(S)_PROXY vars are fine and are what the dataset fetch uses.

## 2. Run terminus-2 through opencodex

```bash
env -u ALL_PROXY -u all_proxy harbor run -d terminal-bench/terminal-bench-2-1 \
  -a terminus-2 \
  -m openai/BLSC/GLM-5.2 \
  --ak api_base=http://127.0.0.1:10109/v1 \
  -l 5 -n 4 -k 1 \
  -o terminal-bench-2-1/jobs -y
```

Notes on the flags:

- `-m openai/<id>` — the `openai/` prefix makes litellm treat the endpoint as
  OpenAI-compatible; the remainder (`BLSC/GLM-5.2`, `deepseek/deepseek-v4-flash`)
  is exactly the model id opencodex routes on. Multiple `-m` flags run one agent
  config per model.
- `--ak api_base=http://127.0.0.1:10109/v1` — terminus-2 `api_base` kwarg.
  No real key is needed for the loopback bind, but litellm still requires a
  non-empty `api_key` for the `openai/` provider, so pass a dummy one:
  `--ak 'llm_kwargs={"api_key":"ocx-loopback"}'`.
- `-l N` — limit to the first N tasks; `-i '<task-glob>'` runs specific tasks
  (task ids are namespaced, e.g. `-i terminal-bench/write-compressor`).
- `-n 4` — concurrent trials (default); `-k 1` — one attempt per task.
- `-o terminal-bench-2-1/jobs` — results land inside this directory.

Optional terminus-2 kwargs (pass with `--ak key=value`):

- `reasoning_effort=high` (values: none/minimal/low/medium/high/xhigh/max)
- `max_turns=200` (cap the episode loop; default unlimited)
- `temperature=0.0`

## Model endpoint reference

From `ocx models` on this host (2026-08-08):

| Provider | Wire model id (use as `openai/<id>`) |
|---|---|
| deepseek | `deepseek/deepseek-v4-flash`, `deepseek/deepseek-v4-pro`, `deepseek-chat`, `deepseek-reasoner` |
| BLSC | `BLSC/DeepSeek-V4-Flash`, `BLSC/DeepSeek-V4-Pro`, `BLSC/GLM-5.2`, `BLSC/Kimi-K2.6`, `BLSC/Kimi-K3`, `BLSC/MiniMax-M3`, `BLSC/Qwen3.6-Plus`, `BLSC/Qwen3.7-Max` |

Cheap first run: `-m openai/deepseek/deepseek-v4-flash`.

## YAML config

[job.example.yaml](./job.example.yaml) is the same setup as a JobConfig file:

```bash
harbor run -c terminal-bench-2-1/job.example.yaml -y
```

Model-specific configs are vendored under
[configs/](./configs/deepseek-v4-flash-max.yaml), and [run.sh](./run.sh) just
dispatches one of them:

```bash
./terminal-bench-2-1/run.sh                                  # configs/deepseek-v4-flash-max.yaml
./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml
./terminal-bench-2-1/run.sh job.example.yaml -l 5 --print-config   # dry-run
```

The default config (`configs/deepseek-v4-flash-max.yaml`) runs all 89 tasks
with terminus-2 on `deepseek/deepseek-v4-flash`, `reasoning_effort: max`, and
no trial retries, and a 2x timeout multiplier (task timeouts are doubled, e.g.
the 900s agent budget becomes 1800s).

### Overriding a config from the CLI

Everything you pass after the config name is merged over the YAML by harbor:

- `--ak key=value` — merge extra agent kwargs, e.g. `--ak reasoning_effort=xhigh`.
- `-r N` — trial retries; `--agent-timeout-multiplier 1.5` — scale task
  timeouts.
- `-o <dir>` / `--job-name <name>` — see next section.

Task filters are the exception: harbor only accepts `-l N` / `-i '<glob>'`
when `-d` is also on the CLI (they replace the YAML's dataset entry, so the
vendored `download_dir` is lost in that case):

```bash
./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml \
  -d terminal-bench/terminal-bench-2-1 -l 10 -i 'terminal-bench/write-*'
```

Swapping the model is the one thing CLI flags do *not* do cleanly: harbor only
honors `-m` together with `-a`, and that path rebuilds the agent config from
scratch, dropping the YAML's `api_base`/`llm_kwargs`. For another model, either
vendor another YAML in `configs/` (recommended) or spell out the endpoint again:

```bash
./terminal-bench-2-1/run.sh -a terminus-2 -m openai/BLSC/GLM-5.2 \
  --ak api_base=http://127.0.0.1:10109/v1 \
  --ak 'llm_kwargs={"api_key":"ocx-loopback"}' \
  --ak reasoning_effort=high -d terminal-bench/terminal-bench-2-1 -l 10
```

Verify before spending quota: `./terminal-bench-2-1/run.sh <config> --print-config`.

### Job directory

Harbor writes each run into `<jobs_dir>/<job_name>/`. `jobs_dir` is the parent
you set in the YAML (or `-o <dir>`); `job_name` defaults to a timestamp, so
runs never collide. The vendored configs leave `job_name` unset on purpose —
every launch creates a fresh `terminal-bench-2-1/jobs/<timestamp>/` with its
own `result.json`, `job.log`, and per-trial directories. If you want a
reusable name (or per-model separation), either set `job_name` in the YAML or
override per run:

```bash
./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml --job-name flash-max-01
./terminal-bench-2-1/run.sh configs/deepseek-v4-flash-max.yaml -o terminal-bench-2-1/jobs/flash-max
```

Note: relative paths in the YAML resolve against the directory you launch
harbor from, so run these from the repo root.

## Results

Each run creates a timestamped job dir under `terminal-bench-2-1/jobs/` with
per-trial trajectories, agent logs, and pass/fail verification results.
Browse them with:

```bash
harbor view -o terminal-bench-2-1/jobs
```

## CLI harnesses later (codex / opencode via opencodex)

When the harness is a CLI agent, harbor installs and runs it inside the task
container. To reach opencodex from there you will need (all still on the
host side, no harbor changes):

1. Make opencodex bind beyond loopback: set `"hostname": "0.0.0.0"` in
   `~/.opencodex/config.json` and restart (`ocx service restart`). opencodex
   keeps an unauthenticated loopback listener for local clients and requires
   a data-plane key for non-loopback callers.
2. Create a data-plane key: `ocx access key create <name>` (header
   `x-opencodex-api-key: <key>`).
3. Point the agent at the host from the container, e.g.
   `--ak api_base=http://192.168.31.65:10109/v1` plus the key in
   `--ak 'llm_kwargs={"extra_headers":{"x-opencodex-api-key":"<key>"}}'`,
   and allow the host in the agent-phase network policy with
   `--allow-agent-host 192.168.31.65` (the container reaches the host via its
   LAN address; the loopback-only bind is invisible from inside Docker).

Until that is set up, terminus-2 (and other harbor-native agents such as
`dspy-rlm`) are the ones that can route through opencodex with zero config.

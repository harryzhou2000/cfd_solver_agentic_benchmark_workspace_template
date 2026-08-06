# Benchmark contestant runtime (Docker)

A reproducible, interactive runtime for benchmark contestants. The image
records the current host environment so every contestant runs inside the same
steady container: opencode (with omo-slim + goal plugin + rate-limit-retry),
codex, the opencodex provider proxy and ocx-relay, codegraph, the CFD build
toolchain (GCC 13, CMake, Ninja, OpenMPI 5.0.9), and the shared DNDSR
externals.

## Startup guide

```bash
# 1. Build the runtime image once (stages host binaries/toolchain; ~5-15 min)
docker/scripts/build.sh

# 2. Create a fresh contestant workspace from an init branch
docker/scripts/setup-workspace.sh codex_gpt56_07 codex/gpt56/init

# 3. Launch the container interactively
docker/scripts/start.sh --workspace ../codex_gpt56_07
#    - live host credentials are mounted, never baked into the image
#    - codex/opencode sessions persist in <workspace>/.sessions/
#    - the container is force-removed on exit (Ctrl-C, SIGTERM, closed terminal)

# 4. Optional: verify a model round-trip without launching interactively
#    (see "Verified end-to-end" below for the full commands)
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -v "$BENCH_ROOT:$BENCH_ROOT" \
  -v "$HOME/.codex:$HOME/.codex" \
  -v "$HOME/.config/opencode:$HOME/.config/opencode" \
  -v "$HOME/.local/share/opencode:$HOME/.local/share/opencode" \
  -v "$HOME/.opencodex:$HOME/.opencodex" -w "$WS" \
  cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

The rest of this file documents the layout, credentials strategy, session
bundling, container lifecycle and the redaction policy.

## Layout

- `Dockerfile` — the image (build context is the repo root).
- `build.sh` — stages host artifacts (opencode/codex/codegraph binaries,
  OpenMPI, externals) into `docker/.context/` (gitignored) and builds the
  image. Staging uses hardlinks when possible, so it does not duplicate disk
  space.
- `configs/` — committed, **secrets-redacted** snapshots of the live
  environment:
  - `opencode/` — `opencode.jsonc` (apiKeys redacted), `oh-my-opencode-slim.json`,
    plugin `package.json`/lockfile, `.oh-my-opencode-slim/`, plus the agent
    packs, commands, MCP, rules, skills, themes and tui config mirrored from
    `~/tools/opencode_config/opencode/`.
  - `codex/` — `config.toml`, opencodex/ocx configs, model catalog, AGENTS.md.
  - `opencodex/` — proxy `config.json` (apiKeys redacted).
  - Regenerate with `scripts/sync-configs.sh`; the script refuses to leave
    unredacted secrets behind.
- `external/` — pointer for the shared DNDSR externals; the real tree is
  staged by `build.sh` and baked into the image at `/opt/external`.
- `entrypoint.sh` — starts the opencodex proxy on 10109 when its config is
  present and the port is free, then execs the requested command.
- `scripts/` — `start.sh` (interactive launcher with workspace-bundled
  sessions and guaranteed container cleanup) and `setup-workspace.sh` (fresh
  contestant workspace: `.sessions/` git-exclusion, `git remote rm origin`,
  `codegraph init`, external symlink).

## Build

```bash
docker/scripts/build.sh            # stages host artifacts, then docker build
IMAGE=cfd-bench:test docker/scripts/build.sh
```

## Run (manual interactive launch)

```bash
docker/scripts/start.sh --workspace ../omo_slim_dsv4_01
```

Mounts (all at the same absolute paths as on the host, so configs that embed
paths keep working):

- the benchmark root (parent of all contestant workspaces, so
  `../opencode_omoslim_deepseek/external` resolves),
- `~/.codex` (config, `auth.json`, plugins; sessions/DBs are *not* shared —
  see below),
- `~/.config/opencode` + `~/.local/share/opencode` (opencode config, plugins,
  auth store; the 33 GB host session DB is *not* shared),
- `~/.opencodex` (proxy config incl. provider keys + state),
- `~/.codegraph` (index cache).

`--image-config` skips mounting the live config dirs and uses the baked
redacted snapshots instead (layout testing only). With `--env-mode` snapshots
plus exported env vars, opencode/opencodex calls can authenticate; codex still
needs its `auth.json` (file-based auth).

### Credentials (safe by construction)

- **Default (recommended): mount the host config dirs.** Real keys never enter
  the repo or the image; they are read from `~/.codex/auth.json`,
  `~/.config/opencode/*` and `~/.local/share/opencode/auth.json` via bind
  mounts at runtime. This is what `start.sh` does.
- **Env vars (opencode + opencodex):** opencode config supports `{env:VAR}`
  placeholders; opencodex resolves `$VAR` / `${VAR}` provider apiKeys from the
  environment on every request (see `resolveEnvValue` in its config module).
  `sync-configs.sh --env-mode` rewrites baked snapshots to env references:
  opencode → `{env:OPENCODE_API_KEY}`, opencodex → per-provider
  `$OPENCODEX_<PROVIDER>_API_KEY` (including `apiKeyPool` entries). Export the
  variables when launching the image.
- **Baked snapshots:** `docker/configs/` is always redacted (`REDACTED`,
  never real keys — `sync-configs.sh` verifies this). Codex credentials have
  no env-placeholder mechanism: auth stays in `auth.json` (mounted) or in a
  custom provider's `env_key`.

### Persistent sessions, bundled with the workspace

`start.sh` (default mode) points each harness at `$WS/.sessions/` inside the
contestant workspace itself:

- `codex` → `CODEX_HOME=$WS/.sessions/codex` (sessions, logs, sqlite DBs),
  with `auth.json`/`config.toml`/profile/caches symlinked from `~/.codex`.
- `opencode` → `XDG_DATA_HOME=$WS/.sessions/opencode-data` (a fresh
  `opencode.db`, logs, storage), with `auth.json`/`account.json` symlinked
  from `~/.local/share/opencode`.

So a contestant run leaves its full session history on disk in the working
directory. `.sessions/` is added to the workspace's `.git/info/exclude`
(`setup-workspace.sh` at creation, `start.sh` as a safety net for existing
workspaces).

### Container lifecycle

The container is always launched with `--rm`, and `start.sh` additionally
traps EXIT/INT/TERM and force-removes the named container, so no `bench-*`
instance survives the launcher even when the client is killed.

`start.sh` also passes `--security-opt seccomp=unconfined
--security-opt apparmor=unconfined`: codex's bundled bubblewrap sandbox needs
user namespaces and mounts that Docker's default profiles block. Without
these flags every codex tool call degrades to "sandbox failed → retry
unsandboxed". The container is the benchmark's isolation boundary; the flags
only relax layers inside it.

## Verified end-to-end (non-interactive)

With the host configs mounted and the host opencodex proxy on 10109:

```bash
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -e CODEX_HOME="$WS/.sessions/codex" \
  -e XDG_DATA_HOME="$WS/.sessions/opencode-data" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -v "$HOME/.codex:$HOME/.codex" \
  -v "$HOME/.config/opencode:$HOME/.config/opencode" \
  -v "$HOME/.local/share/opencode:$HOME/.local/share/opencode" \
  -v "$HOME/.opencodex:$HOME/.opencodex" -w "$WS" \
  cfd-bench:latest codex exec --json -p ocx --skip-git-repo-check \
    "Reply with exactly: PONG"

docker run ... cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

Both return `PONG` through the real provider stack; codex sandboxed tool
execution (`echo codex-e2e-ok`) also succeeds with the security opts above.
Verified with codex-cli 0.146.0 + opencode 1.18.11 in the image.

### Known noise / tuning

- Codex 0.146 tries the Responses **WebSocket** transport first; the opencodex
  proxy has it off by default and answers `426 Upgrade Required`, costing
  ~40 s of retries before codex falls back to HTTP/SSE. To skip it, replace
  the built-in `openai` provider in the ocx profile with a custom provider:
  `model_provider = "ocx"` plus
  `[model_providers.ocx] name="OpenCodex" base_url="http://127.0.0.1:10109/v1"
  wire_api="responses" requires_openai_auth=true supports_websockets=false`.
  (Codex refuses to override the reserved built-in `openai` provider id.)
- Codex emits a non-fatal `rmcp ... chatgpt.com/backend-api/ps/mcp` error on
  this network (cloud MCP unreachable); the run completes normally.

## Fresh contestant workspace

```bash
docker/scripts/setup-workspace.sh codex_gpt56_07 codex/gpt56/init
docker/scripts/start.sh --workspace ../codex_gpt56_07
```

Inside the container, contestants work interactively: `codex`, `opencode`
(TUI), `ocx`, `codegraph`, `mpic++`, `cmake`, etc. are all on `PATH`.

## Redaction

Never commit real credentials: `apiKey`/`sk-*` values are replaced with
`REDACTED` in `configs/`. `auth.json`, `admin-api-token`, session databases
and logs are never copied — they stay on the host and are only mounted.

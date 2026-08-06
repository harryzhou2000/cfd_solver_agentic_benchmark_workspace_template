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
docker/build.sh

# 2. Create a fresh contestant workspace from an init branch
docker/scripts/setup-workspace.sh ../codex_gpt56_07 codex/gpt56/init
#    (the workspace path is used as-is: absolute, or relative to the cwd you
#    run the script from; it is not prefixed with $BENCH_ROOT)

# 3. Launch the container interactively
docker/scripts/start.sh --workspace ../codex_gpt56_07
#    - configs come from the image (built by build.sh from your live ~/*
#      configs), with a per-workspace snapshot in <workspace>/.sessions/
#    - codex home is <workspace>/.sessions/codex mounted at /home/harry/.codex
#    - the container is force-removed on exit (Ctrl-C, SIGTERM, closed terminal)

# 4. Optional: verify a model round-trip without launching interactively
#    (see "Verified end-to-end" below for the full commands)
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

The rest of this file documents the layout, credentials strategy, session
bundling, container lifecycle and the redaction policy.

## Layout

- `Dockerfile` — the image (build context is the repo root).
- `build.sh` — stages host artifacts (opencode/codex/codegraph binaries,
  OpenMPI, externals) **and the live user configs** into `docker/.context/`
  (gitignored) and builds the image. Staging uses hardlinks when possible, so
  it does not duplicate disk space. The config mirror lands in
  `docker/.context/configs/`:
  - `codex/` — `~/.codex` minus runtime state (config.toml, ocx profile,
    auth.json, catalog, skills/plugins/rules, caches);
  - `opencodex/` — `~/.opencodex` minus logs/state (config.json, auth.json,
    codex accounts, admin token);
  - `opencode/` — `~/.config/opencode` minus node_modules/logs/backups;
  - `opencode-data/` — the opencode auth store (`auth.json`, `account.json`;
    never the 33 GB session DB).
- `configs/` — committed, **secrets-redacted** reference snapshots of the
  same environment:
  - `opencode/` — `opencode.jsonc` (apiKeys redacted), `oh-my-opencode-slim.json`,
    plugin `package.json`/lockfile, `.oh-my-opencode-slim/`, plus the agent
    packs, commands, MCP, rules, skills, themes and tui config mirrored from
    `~/tools/opencode_config/opencode/`.
  - `codex/` — `config.toml`, opencodex/ocx configs, model catalog, AGENTS.md.
  - `opencodex/` — proxy `config.json` (apiKeys redacted).
  - Regenerate with `scripts/sync-configs.sh`; the script refuses to leave
    unredacted secrets behind. These are *not* baked into the image — the
    image uses the real `docker/.context/configs/` mirror.
- `external/` — pointer for the shared DNDSR externals; the real tree is
  staged by `build.sh` and baked into the image at `/opt/external`.
- `entrypoint.sh` — starts the opencodex proxy on 10109 when its config is
  present and the port is free, then execs the requested command.
- `scripts/` — `start.sh` (interactive launcher with workspace-bundled
  sessions and guaranteed container cleanup) and `setup-workspace.sh` (fresh
  contestant workspace from a path: `.sessions/` git-exclusion, `git remote
  rm origin`, `codegraph init`, external symlink).

### Config layering inside the container

The image is self-contained: your user-level configs are mirrored at build
time and are effective at their usual inside-docker-home paths.

1. **Baked (build time):** `build.sh` mirrors the live host configs into
   `docker/.context/configs/` (gitignored, real keys) and the `Dockerfile`
   `COPY`s them to `/home/harry/.codex`, `/home/harry/.opencodex`,
   `/home/harry/.config/opencode` and `/home/harry/.local/share/opencode`
   (auth store only). Rebuild the image to refresh the mirror.
2. **Per-workspace snapshot (runtime, `start.sh` default):** at launch the
   config set is copied (not symlinked) into `$WS/.sessions/` as a
   per-workspace record — the codex config set into `.sessions/codex/`, and a
   **redacted** opencodex config into `.sessions/opencodex/config.json`.
   Credential files (`~/.codex/auth.json`, the opencode auth store) are
   **never copied**: they are bind-mounted from the host at runtime, so the
   workspace record stays credential-free. Exec-policy `rules/` files may
   embed API keys in allow-rule patterns; the record keeps a redacted copy
   and the real rules are bind-mounted for codex.
3. **Effective inside the container:** the codex snapshot is bind-mounted at
   `/home/harry/.codex` (with `CODEX_HOME=/home/harry/.codex`), so codex
   reads and writes at the inside-docker-home path while everything persists
   in the workspace. opencode/opencodex use the baked configs directly.

How each harness picks its config:

- **opencode** — `~/.config/opencode/opencode.jsonc` (+ omo-slim plugin,
  commands, skills from the same dir); auth from the data dir
  (`$XDG_DATA_HOME/opencode/auth.json` — the host auth store bind-mounted
  into the workspace bundle).
- **codex** — `$CODEX_HOME/config.toml` (= `/home/harry/.codex`, the
  workspace snapshot), plus profiles next to it. The user-level **ocx
  profile** (`ocx.config.toml` — deepseek-v4-flash routed through the
  opencodex proxy at 127.0.0.1:10109) is part of every snapshot; launch
  `codex -p ocx`, or `start.sh --harness codex --codex-profile ocx`.
- **opencodex** — `~/.opencodex/config.json` (baked, real; redacted copy in
  the workspace record). The entrypoint starts the proxy on 10109 only when
  the port is free; with `--network host` the host-side proxy (if running) is
  used as-is.

Note: codex **project-local** `.codex/config.toml` files cannot set provider
routing — codex ignores `model_provider`, `model_providers` and
`openai_base_url` there by design. The ocx routing therefore lives in the
user-level profile, not in contestant repos.

## Build

```bash
docker/build.sh                    # stages host artifacts + live configs, then docker build
IMAGE=cfd-bench:test docker/build.sh
```

### Robustness on other machines

- **Missing host tools:** `build.sh` auto-discovers opencode/codex/codegraph
  (default paths first, then `PATH`). Anything still missing is staged as an
  empty placeholder, so the image build always succeeds — the image just ends
  up without that tool (a warning is printed).
- **Missing submodules:** the build fails fast with a clear
  `git submodule update --init --recursive` hint instead of a cryptic COPY
  error.
- **Missing user configs:** `start.sh` detects whether the host has
  `~/.codex` configs and the opencode auth store. When absent, it skips the
  workspace snapshot and falls back to the baked image configs (the image is
  self-contained); codex sessions are then ephemeral. `--mount-host-configs`
  only mounts host dirs that actually exist.
- **No proxy on the host:** both scripts only source `~/.setproxy.sh` when it
  exists; everything works with direct connectivity too.

## Run (manual interactive launch)

```bash
docker/scripts/start.sh --workspace ../omo_slim_dsv4_01
```

Default mode mounts:

- the benchmark root (parent of all contestant workspaces, so
  `../opencode_omoslim_deepseek/external` resolves),
- `$WS/.sessions/codex` → `/home/harry/.codex` (the per-workspace codex home:
  config snapshot + persistent sessions),
- `~/.codegraph` (index cache).

`--image-config` skips the workspace session bundle entirely and uses the
baked configs directly (sessions are ephemeral; for bare/CI runs).
`--mount-host-configs` additionally bind-mounts the live host
`~/.config/opencode`, `~/.local/share/opencode` and `~/.opencodex` over the
baked ones, for live-edit workflows without an image rebuild.
If the host has no `~/.codex` or opencode auth store, `start.sh` falls back
to the baked configs automatically (sessions become ephemeral).

### Network / proxy

- The container runs with `--network host`, so it shares the host's network
  stack; LAN and loopback proxy endpoints are reachable as-is.
- `start.sh` and `build.sh` source `~/.setproxy.sh` when present (override
  with `PROXY_SCRIPT=/path`) and forward `HTTP_PROXY` / `HTTPS_PROXY` /
  `ALL_PROXY` / `NO_PROXY` (upper- and lowercase) into the container,
  respectively as `docker build --build-arg`s. `localhost`, `127.0.0.1` and
  `::1` are appended to `NO_PROXY` so the opencodex proxy on
  127.0.0.1:10109 is never proxied.
- Docker does not inherit the shell environment, which is why the scripts
  forward the variables explicitly. For a manual `docker run`, pass
  `-e HTTP_PROXY=... -e HTTPS_PROXY=...` yourself.

### Credentials (safe by construction)

- **Baked mirror (default):** `build.sh` stages the real host configs
  (including `auth.json`, opencodex `config.json`, opencode apiKeys) into
  gitignored `docker/.context/configs/` and the image bakes them into the
  container home. The image is a personal artifact — rebuild it to refresh
  credentials. Nothing real is ever committed: `docker/configs/` stays
  redacted and `sync-configs.sh` verifies that.
- **Per-workspace snapshot:** each `start.sh` launch copies the config set
  into `$WS/.sessions/` (record + effective codex home via the mount).
  **Credentials are excluded from the snapshot**: `auth.json` files are
  bind-mounted from the host at runtime and the opencodex record is
  redacted, so the workspace never stores keys.
- **Env vars (opencode + opencodex):** opencode config supports `{env:VAR}`
  placeholders; opencodex resolves `$VAR` / `${VAR}` provider apiKeys from the
  environment on every request (see `resolveEnvValue` in its config module).
  `sync-configs.sh --env-mode` rewrites baked snapshots to env references:
  opencode → `{env:OPENCODE_API_KEY}`, opencodex → per-provider
  `$OPENCODEX_<PROVIDER>_API_KEY` (including `apiKeyPool` entries). Export the
  variables when launching the image.
- Codex credentials have no env-placeholder mechanism: auth stays in
  `auth.json` (baked/snapshotted) or in a custom provider's `env_key`.

### Persistent sessions, bundled with the workspace

`start.sh` (default mode) points each harness at `$WS/.sessions/` inside the
contestant workspace itself:

- `codex` → `$WS/.sessions/codex` mounted at `/home/harry/.codex` with
  `CODEX_HOME=/home/harry/.codex` (config snapshot + sessions, logs, sqlite
  DBs all persist in the workspace; the effective path is the
  inside-docker-home one; `auth.json` is bind-mounted from `~/.codex`, never
  stored in the snapshot; `rules/` likewise bind-mounted with a redacted
  record copy).
- `opencode` → `XDG_DATA_HOME=$WS/.sessions/opencode-data` (a fresh
  `opencode.db`, logs, storage), with `auth.json`/`account.json`
  bind-mounted from `~/.local/share/opencode` (never copied).
- `opencodex` → `$WS/.sessions/opencodex/config.json` (redacted record; the
  service itself reads the baked `~/.opencodex/config.json`).

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

With the baked configs (and the host opencodex proxy on 10109 when codex
routes through it):

```bash
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest codex exec --json -p ocx --skip-git-repo-check \
    "Reply with exactly: PONG"

docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest opencode run --format json \
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
docker/scripts/setup-workspace.sh ../codex_gpt56_07 codex/gpt56/init
docker/scripts/start.sh --workspace ../codex_gpt56_07
```

Inside the container, contestants work interactively: `codex`, `opencode`
(TUI), `ocx`, `codegraph`, `mpic++`, `cmake`, etc. are all on `PATH`.
For codex-through-opencodex, launch `codex -p ocx` or use
`start.sh --harness codex --codex-profile ocx`.

## Redaction

Never commit real credentials: `apiKey`/`sk-*` values are replaced with
`REDACTED` in the committed `configs/`. The build-time mirror
(`docker/.context/configs/`) contains the real keys and is gitignored; it is
only ever used to build the local image. Session databases and logs are never
mirrored.

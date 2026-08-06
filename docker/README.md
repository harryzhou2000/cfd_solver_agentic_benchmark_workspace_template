# Benchmark contestant runtime (Docker)

A reproducible, interactive runtime for benchmark contestants. Every tool is
installed fresh in the image from its official source at a version pinned to
this machine — never copied from the host (the host could be malformed):
opencode 1.18.11, codex-cli 0.146.0, the opencodex provider proxy and
ocx-relay (pinned commits), codegraph 1.2.0, Node 24.18.0, bun 1.3.14, uv
0.12.1, the CFD build toolchain (GCC 13, CMake, Ninja, OpenMPI 4.1.6 from
apt), and the shared DNDSR externals (header-only
bundle at the pinned release tag, plus cfd_externals built from source).
opencode ships with the two pinned plugins installed via opencode's official
installer: the goal plugin (at the fork's PR-tip commit) and
oh-my-opencode-slim@2.2.10. User configs are never baked into the image; they
are injected at container start (see below).

## Startup guide

```bash
# 1. Build the runtime image once (fresh official installs, pinned versions;
#    ~10-60 min depending on network and JOBS; only the proxy env vars are
#    forwarded — nothing is copied from the host)
docker/build.sh

# 2. Create a fresh contestant workspace from an init branch
docker/scripts/setup-workspace.sh ../codex_gpt56_07 codex/gpt56/init
#    (the workspace path is used as-is: absolute, or relative to the cwd you
#    run the script from; it is not prefixed with $BENCH_ROOT)

# 3. Launch the container interactively
docker/scripts/start.sh --workspace ../codex_gpt56_07
#    - the vendored config stack (docker/configs/) is installed at start,
#      with per-workspace copies in <workspace>/.sessions/:
#        codex     .sessions/codex           -> /home/harry/.codex
#        opencode  .sessions/opencode-config -> /home/harry/.config/opencode
#        opencodex .sessions/opencodex       -> /home/harry/.opencodex
#    - credentials: export the stack's env vars, or pass --host-credentials
#    - the container is force-removed on exit (Ctrl-C, SIGTERM, closed terminal)

# 4. Optional: verify a model round-trip without launching interactively.
#    On this host the opencodex proxy listens on 10109, so the same port is
#    forced inside the container (override with OCX_PORT if yours differs;
#    when unset, the stack config's port is used, default 10100).
#    Bare `docker run` has no configs — mount the vendored stack (see
#    "Verified end-to-end" below for the full commands).
#    (see "Verified end-to-end" below for the full commands)
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" -e OCX_PORT=10109 \
  -v "$(pwd)/docker/configs/codex:/home/harry/.codex" \
  -v "$(pwd)/docker/configs/opencode:/home/harry/.config/opencode" \
  -v "$(pwd)/docker/configs/opencodex:/home/harry/.opencodex" \
  -e OPENCODE_API_KEY_DEEPSEEK="${OPENCODE_API_KEY_DEEPSEEK:?export the referenced env vars}" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

The rest of this file documents the layout, credentials strategy, session
bundling, container lifecycle and the redaction policy.

## Layout

- `Dockerfile` — the image (build context is the repo root). Installs every
  tool fresh from its official source, pinned to this machine's versions:
  Node 24.18.0 (nodejs.org), bun 1.3.14 (bun.sh installer), uv 0.12.1
  (astral release), OpenMPI 4.1.6 (apt: `openmpi-bin` + `libopenmpi-dev`),
  opencode 1.18.11 (`opencode.ai/install --version 1.18.11`),
  codex-cli 0.146.0 (openai/codex release tarball), codegraph 1.2.0 (npm),
  opencodex + ocx-relay (fresh clones at pinned commits), and the DNDSR
  externals (header-only release tarball + cfd_externals built from source).
- `build.sh` — thin build wrapper: `docker build` plus proxy build-args.
  There is **no staging** anymore: nothing is copied from the host into the
  build (the old `docker/.context/` staging area is obsolete; the local dir
  can be deleted).
- `opencode-plugins.json` — pinned plugin manifest: the goal plugin at the
  fork's PR-tip commit and `oh-my-opencode-slim@2.2.10`.
- `scripts/install-opencode-plugins.sh` — installs the manifest into the
  image with opencode's official `opencode plugin -g` command (fetches from
  npm/GitHub only; never from the host).
- `configs/` — committed, **secrets-redacted** reference snapshots of the
  host environment (documentation only; NOT used by the image):
  - `opencode/` — `opencode.jsonc` (apiKeys redacted), `oh-my-opencode-slim.json`,
    plugin `package.json`/lockfile, `.oh-my-opencode-slim/`, plus the agent
    packs, commands, MCP, rules, skills, themes and tui config mirrored from
    `~/tools/opencode_config/opencode/`.
  - `codex/` — `config.toml`, opencodex/ocx configs, model catalog, AGENTS.md.
  - `opencodex/` — proxy `config.json` (apiKeys redacted).
  - Regenerate with `scripts/sync-configs.sh`; the script refuses to leave
    unredacted secrets behind.
- `external/` — pointer for the shared DNDSR externals. The real tree is
  built inside the image at `/opt/external` (header-onlys + cfd_externals
  from source, per the DNDSR recipe — before the harnesses, right after the
  build stack (incl. OpenMPI) and uv), and `setup-workspace.sh` symlinks every
  contestant workspace's `external/` to `/opt/external`. No host binary tree
  is staged or referenced.
- `entrypoint.sh` — starts the opencodex proxy when its config is present and
  the port is free, then execs the requested command. The probe port comes
  from `$OCX_PORT` (also passed to `ocx start --port`), else `config.json`
  `.port`, else 10100 (the opencodex default).
- `scripts/` — `start.sh` (interactive launcher with workspace-bundled
  sessions and guaranteed container cleanup) and `setup-workspace.sh` (fresh
  contestant workspace from a path: `.sessions/` git-exclusion, `git remote
  rm origin`, `codegraph init`, external symlink).

### Config layering inside the container

The image carries **no user configs** — only a pristine global opencode
config written by the plugin installer (`~/.config/opencode/opencode.jsonc`
with the two pinned plugins), whose packages live in the image's plugin store
(`~/.cache/opencode/packages`).

The **vendored config stack** — `<repo>/docker/configs/`, gathered from the
current host with `sync-configs.sh` and committed credential-free — is the
config source at container start (`start.sh` default). Override the location
with `CONFIG_STACK=/path/to/stack` (an equivalent user-configured directory).
At start the stack is copied into `$WS/.sessions/` (per-workspace record +
persistent session storage) and mounted at the inside-docker-home paths:

1. **codex** — `configs/codex/` → `$WS/.sessions/codex/` →
   `/home/harry/.codex` (`CODEX_HOME=/home/harry/.codex`): config.toml, the
   ocx profiles, catalog, AGENTS.md. Sessions/logs/DBs persist in the
   workspace copy.
2. **opencode** — `configs/opencode/` → `$WS/.sessions/opencode-config/` →
   `/home/harry/.config/opencode`: opencode.jsonc (apiKeys as
   `{env:OPENCODE_API_KEY_<PROVIDER>}` references), agents, commands, skills,
   MCP, themes. The data dir (`XDG_DATA_HOME=$WS/.sessions/opencode-data`)
   holds sessions/DB and the auth store (fresh or bind-mounted).
3. **opencodex** — `configs/opencodex/` → `$WS/.sessions/opencodex/` →
   `/home/harry/.opencodex`: config.json (apiKeys as
   `$OPENCODEX_<PROVIDER>_API_KEY` references). Runtime state (usage,
   artifacts, sqlite) persists in the workspace copy.

The live host config stack is **never** used as the config source (no
fallback). Credentials are supplied separately: export the referenced env
vars on this host (forwarded into the container), or pass
`--host-credentials` to export the real keys from the live host configs
read-only. `--mount-host-configs` is the explicit, non-reproducible escape
hatch that mounts the live host dirs instead.

How each harness picks its config:

- **opencode** — the vendored `opencode.jsonc` at
  `/home/harry/.config/opencode` (+ omo-slim plugin, commands, skills from
  the same dir); auth from the data dir (`$XDG_DATA_HOME/opencode/`).
- **codex** — `$CODEX_HOME/config.toml` (= `/home/harry/.codex`, the
  vendored stack copy), plus profiles next to it. The user-level **ocx
  profile** (`ocx.config.toml` — deepseek-v4-flash routed through the
  opencodex proxy at 127.0.0.1:10109) is part of the stack; launch
  `codex -p ocx`, or `start.sh --harness codex --codex-profile ocx`.
- **opencodex** — the vendored `~/.opencodex/config.json`. The entrypoint
  starts the proxy on the config's port (10109 on this host) only when the
  port is free; with `--network host` the host-side proxy (if running) is
  used as-is.

Note: codex **project-local** `.codex/config.toml` files cannot set provider
routing — codex ignores `model_provider`, `model_providers` and
`openai_base_url` there by design. The ocx routing therefore lives in the
user-level profile, not in contestant repos.

## Build

```bash
docker/build.sh                    # docker build (fresh official installs, pinned versions)
IMAGE=cfd-bench:test docker/build.sh
JOBS=8 docker/build.sh             # more parallel build jobs (default 4)
```

### Robustness on other machines

- **Missing host tools:** not applicable — the image installs every tool
  fresh from its official source (apt/npm/GitHub/nodejs.org), so a
  host without codex/opencode/opencodex builds the same image. The build
  needs network access (and a proxy, if the host requires one — see below).
- **Missing pinned artifacts:** pinned commits/tags are resolved via the
  GitHub API check at fetch time; if a pinned ref is ever removed, the build
  fails loudly at the `git fetch`/`curl` step instead of silently using a
  different version.
- **Missing vendored configs:** `start.sh` requires the config stack
  (`<repo>/docker/configs`, or `CONFIG_STACK=`); if it is missing/incomplete
  it fails with a hint to run `sync-configs.sh --env-mode`. There is no
  fallback to the live host configs and no fallback to the image.
- **No proxy on the host:** both scripts only use the already-exported proxy
  env vars (`HTTP_PROXY` etc., no proxy script is sourced); everything works
  with direct connectivity too.

## Run (manual interactive launch)

```bash
docker/scripts/start.sh --workspace ../omo_slim_dsv4_01
```

Default mode mounts:

- the benchmark root (parent of all contestant workspaces, so
  workspace-relative paths like `../opencode_omoslim_deepseek/external`
  resolve),
- the vendored config stack (default `<repo>/docker/configs`, override with
  `CONFIG_STACK=/path/to/stack`), copied into `$WS/.sessions/` and mounted
  at the inside-docker-home paths,
- `$WS/.sessions/codex` → `/home/harry/.codex`,
- `$WS/.sessions/opencode-config` → `/home/harry/.config/opencode`,
- `$WS/.sessions/opencodex` → `/home/harry/.opencodex`,
- `$WS/.sessions/opencode-data` → the opencode data dir
  (`XDG_DATA_HOME`; fresh DB; auth via env or `--host-credentials`),
- `~/.codegraph` (index cache).

Credentials are **never** part of the stack. Two modes at start:

- **Env as credentials (default):** export the vars the stack references —
  `OPENCODE_API_KEY_<PROVIDER>` for opencode, `OPENCODEX_<PROVIDER>_API_KEY`
  for opencodex — and `start.sh` forwards any exported `OPENCODE_*` /
  `OPENCODEX_*` vars into the container. Codex auth stays file-based:
  `auth.json` (via `--host-credentials`) or `codex login` inside the
  container.
- **Extract credentials (`--host-credentials`):** `start.sh` reads THIS
  host's live configs read-only and turns their real keys into the
  container env under exactly the referenced names (opencode:
  `OPENCODE_API_KEY_<PROVIDER>`; opencodex: `OPENCODEX_<PROVIDER>_API_KEY`),
  and bind-mounts `~/.codex/auth.json` + the opencode auth store. Nothing is
  written to the workspace; the keys only exist in the container env.

`--image-config` skips the config-stack injection and the workspace session
bundle entirely, using the image's pristine state (sessions are ephemeral;
for bare/CI runs). `--mount-host-configs` bind-mounts the live host config
dirs (`~/.codex`, `~/.config/opencode`, `~/.local/share/opencode`,
`~/.opencodex`) over the stack — the explicit non-reproducible escape hatch
for live-edit workflows.

### Network / proxy

- The container runs with `--network host`, so it shares the host's network
  stack; LAN **and loopback** proxy endpoints are reachable as-is — the
  container's `127.0.0.1` is the host's loopback, so a proxy bound to
  `127.0.0.1:PORT` on the host works without changes (verified live).
- `start.sh` and `build.sh` use the already-exported proxy env vars only —
  no `~/.setproxy.sh` (or any other proxy script) is sourced. `start.sh`
  forwards `HTTP_PROXY` / `HTTPS_PROXY` / `ALL_PROXY` / `NO_PROXY`
  (upper- and lowercase) into the container. `localhost`, `127.0.0.1` and
  `::1` are appended to `NO_PROXY` so the opencodex proxy on 127.0.0.1:10109
  is never proxied.
- Docker does not inherit the shell environment, which is why the scripts
  forward the variables explicitly. For a manual `docker run`, pass
  `-e HTTP_PROXY=... -e HTTPS_PROXY=...` yourself.
- Image builds pass proxy build-args and `--network host` **only when the
  proxy is on the host loopback** (build containers cannot reach the host's
  `127.0.0.1` otherwise). LAN proxies are skipped by default — direct
  connectivity avoids flaky apt/npm failures through the proxy — force them
  with `BUILD_PROXY=1`.
- opencodex autostart is skipped when the port is already occupied — e.g.
  with `--network host` and a host-side daemon on the same port. Override the
  probe/service port with `OCX_PORT` (default: `config.json` `.port` or
  10100).

### Credentials (safe by construction)

- **Vendored stack is credential-free by construction:** `sync-configs.sh`
  redacts every secret and the verifier refuses to commit unredacted keys.
  With `--env-mode` the apiKeys become env references instead of
  `REDACTED`: opencode → per-provider `{env:OPENCODE_API_KEY_<PROVIDER>}`
  (opencode's native placeholder), opencodex → per-provider
  `$OPENCODEX_<PROVIDER>_API_KEY` (opencodex resolves `$VAR` / `${VAR}`
  apiKeys on every request, including `apiKeyPool` entries).
- **Env as credentials (default):** export the referenced vars and
  `start.sh` forwards them into the container (`-e VAR`). Nothing is ever
  written to the workspace.
- **Extract credentials (`--host-credentials`):** `start.sh` reads the live
  host configs (`~/.config/opencode/opencode.jsonc`,
  `~/.opencodex/config.json`) read-only, exports the real keys into the
  container env under the same names the stack references, and bind-mounts
  `~/.codex/auth.json` plus the opencode auth store
  (`~/.local/share/opencode/{auth,account}.json`). The keys exist only in
  the container env / mount — never in the workspace record.
- Codex credentials have no env-placeholder mechanism: auth stays in
  `auth.json` (bind-mounted with `--host-credentials`, or `codex login`
  inside the container) or in a custom provider's `env_key`.

### Persistent sessions, bundled with the workspace

`start.sh` (default mode) points each harness at `$WS/.sessions/` inside the
contestant workspace itself:

- `codex` → `$WS/.sessions/codex` mounted at `/home/harry/.codex` with
  `CODEX_HOME=/home/harry/.codex` (vendored config copy + sessions, logs,
  sqlite DBs all persist in the workspace; with `--host-credentials`,
  `auth.json` is bind-mounted from `~/.codex`, never stored).
- `opencode` → `XDG_DATA_HOME=$WS/.sessions/opencode-data` (a fresh
  `opencode.db`, logs, storage), with `auth.json`/`account.json` optionally
  bind-mounted from `~/.local/share/opencode` (`--host-credentials`; never
  copied).
- `opencodex` → `$WS/.sessions/opencodex` mounted at `/home/harry/.opencodex`
  (vendored config copy + runtime state).

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

With the vendored config stack mounted and the credential env vars set (and
the host opencodex proxy on 10109 when codex routes through it):

```bash
REPO="$(pwd)"   # the manager repo (holds the vendored config stack)
# Mount the stack from a temp copy (start.sh copies it into $WS/.sessions/;
# here we only need it read-write for codex's auth.json placeholder).
CFG="$(mktemp -d)" && cp -a "$REPO/docker/configs/." "$CFG/"
touch "$CFG/codex/auth.json"   # non-credential placeholder; real file mounts over it

docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" \
  -v "$CFG/codex:/home/harry/.codex" \
  -v "$CFG/opencode:/home/harry/.config/opencode" \
  -v "$CFG/opencodex:/home/harry/.opencodex" \
  -v "$HOME/.codex/auth.json:/home/harry/.codex/auth.json" \
  -e OPENCODE_API_KEY_DEEPSEEK="${OPENCODE_API_KEY_DEEPSEEK:?export the referenced env vars}" \
  -e OPENCODEX_DEEPSEEK_API_KEY="${OPENCODEX_DEEPSEEK_API_KEY:?}" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest codex exec --json -p ocx --skip-git-repo-check \
    "Reply with exactly: PONG"

docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user "$(id -u):$(id -g)" \
  -e HOME="$HOME" \
  -v "$CFG/codex:/home/harry/.codex" \
  -v "$CFG/opencode:/home/harry/.config/opencode" \
  -v "$CFG/opencodex:/home/harry/.opencodex" \
  -e OPENCODE_API_KEY_DEEPSEEK="${OPENCODE_API_KEY_DEEPSEEK:?export the referenced env vars}" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

The commands mirror what `start.sh` does in its default mode (vendored stack
at the inside-docker-home paths + credential env vars + the real codex
auth.json mounted over a placeholder). `start.sh` copies the stack into
`$WS/.sessions/` instead of a temp dir, so contestant runs persist everything
in the workspace. Both return `PONG` through the real provider stack; codex
sandboxed tool execution (`echo codex-e2e-ok`) also succeeds with the
security opts above. Verified with codex-cli 0.146.0 + opencode 1.18.11 in
the image.

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
`REDACTED` (or env references with `--env-mode`) in the committed
`configs/`; `sync-configs.sh` verifies no unredacted key survives. The image
is built without any host configs, so there is no build-time key mirror at
all (the old `docker/.context/` staging area is obsolete and can be
deleted). Real keys enter a container only at start time, via exported env
vars or `--host-credentials` binds, and are never written to the workspace.
Session databases and logs are never vendored.

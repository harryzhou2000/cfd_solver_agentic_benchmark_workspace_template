# Benchmark contestant runtime (Docker)

A reproducible, interactive runtime for benchmark contestants. Every tool is
installed fresh in the image from its official source at a version pinned to
this machine — never copied from the host (the host could be malformed):
opencode 1.18.14, codex-cli 0.146.0, the opencodex provider proxy and
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
docker/scripts/setup-workspace.sh codex/gpt56/08 codex/gpt56/init
#    (relative paths resolve under <repo>/workspace/ — git-ignored; absolute
#    paths are used as-is. workspace/codex/gpt56/08 is created here)

# 3. Launch the container interactively
docker/scripts/start.sh --workspace workspace/codex/gpt56/08
#    - the vendored config stack (docker/configs/) is installed at start,
#      with per-workspace copies in <workspace>/.sessions/:
#        bash      .sessions/bash            -> ~/.bashrc/.profile/... (default Ubuntu setup)
#        codex     .sessions/codex           -> /home/cfd_agent/.codex
#        opencode  .sessions/opencode-config -> /home/cfd_agent/.config/opencode
#        opencodex .sessions/opencodex       -> /home/cfd_agent/.opencodex
#    - credentials: export the stack's env vars, or pass --host-credentials
#    - the container is force-removed on exit (Ctrl-C, SIGTERM, closed terminal)

# 4. Optional: verify a model round-trip without launching interactively.
#    opencodex is a router/proxy, not a harness: by default the container
#    does NOT start one and relies on the host-hosted ocx service
#    (--network host reaches it on 127.0.0.1:10109 on this host; OCX_PORT
#    only matters when OPENCODEX_AUTOSTART=1 starts a container-local proxy).
#    Bare `docker run` has no configs — mount the vendored stack (see
#    "Verified end-to-end" below for the full commands).
#    (see "Verified end-to-end" below for the full commands)
docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user root:root \
  -e HOME=/home/cfd_agent -e OCX_PORT=10109 \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$(pwd)/docker/configs/codex:/home/cfd_agent/.codex" \
  -v "$(pwd)/docker/configs/opencode:/home/cfd_agent/.config/opencode" \
  -v "$(pwd)/docker/configs/opencodex:/home/cfd_agent/.opencodex" \
  -e OPENCODE_API_KEY_DEEPSEEK="${OPENCODE_API_KEY_DEEPSEEK:?export the referenced env vars}" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest opencode run --format json \
  -m deepseek/deepseek-v4-flash "Reply with exactly: PONG"
```

### start.sh parameters and environment

Everything below is documented here so a run is reproducible from the
command line alone. `start.sh` prints the full effective configuration
(container name, cpus, mounts) and announces **every env var** it passes into
the container before launching — values are masked to head+tail (e.g.
`sk-abc…wxyz`), so exported credential keys are identifiable without being
leaked to the terminal.

| flag / env | default | effect |
|---|---|---|
| `--workspace DIR` (or first positional) | cwd | contestant workspace; must exist. Absolute or relative (used as-is, no prefix) |
| `--harness shell\|codex\|opencode` | `shell` | command to exec: interactive bash, `codex`, or `opencode` |
| `--codex-profile ocx` | – | run codex as `codex -p ocx` (route through the opencodex proxy) |
| `--name NAME` / `-n` | `bench-<workspace basename>` | container name, so `docker ps` is readable |
| `--cpus N` | `4` | CPU quota via docker `--cpus` (N cores' worth of time); env `CPUS` also works |
| `--host-credentials` | off | read real apiKeys from the live host configs (read-only) and pass them as env vars / auth-file binds |
| `--image-config` | off | skip the config stack entirely (pristine image state, ephemeral sessions) |
| `--mount-host-configs` | off | bind-mount live host config dirs over the stack (non-reproducible escape hatch) |
| `CONFIG_STACK=/path` | `<repo>/docker/configs` | vendored config stack installed into `$WS/.sessions` at start |
| `IMAGE=name` | `cfd-bench:latest` | image to run |
| `WORKSPACE=/path` | `/workspace` | the in-container workspace path: `start.sh` mounts the workspace there and the entrypoint `cd`s into it before exec (docker `-w` too); no host path is ever mounted |
| `OCX_PORT=10109` | stack config's `.port`, else `10100` | opencodex service port the container relies on; with the default host-hosted setup it must match the host-side ocx (e.g. 10109). Also the port used when `OPENCODEX_AUTOSTART=1` starts a container-local proxy |
| `OPENCODEX_AUTOSTART=1` | `0` | opt into starting a container-local opencodex proxy; the default is to rely on the host-hosted ocx service (opencodex is a router/proxy, not a harness) |
| `OPENCODE_API_KEY_*`, `OPENCODEX_*` | – | credential env refs the vendored stack expects; export them or use `--host-credentials` |
| `HTTP_PROXY`/`HTTPS_PROXY`/`ALL_PROXY`/`NO_PROXY` (+lowercase) | – | forwarded into the container when already exported on the host |

`HOST_UID`/`HOST_GID` are always derived from the invoking user (`id -u` /
`id -g`): the entrypoint remaps the image's generic `cfd_agent` user to them
(enroot-style), so files created in the container are owned by you and the
mounted host dirs work unchanged. Example with everything explicit:

```bash
CPUS=8 OCX_PORT=10109 \
docker/scripts/start.sh \
  --workspace workspace/codex/gpt56/08 \
  --harness codex --codex-profile ocx \
  --name gpt56-07 --cpus 8 \
  --host-credentials
```

The rest of this file documents the layout, credentials strategy, session
bundling, container lifecycle and the redaction policy.

## Layout

- `Dockerfile` — the image (build context is the repo root). Installs every
  tool fresh from its official source, pinned to this machine's versions:
  Node 24.18.0 (nodejs.org), bun 1.3.14 (bun.sh installer), uv 0.12.1
  (astral release), OpenMPI 4.1.6 (apt: `openmpi-bin` + `libopenmpi-dev`),
  opencode 1.18.14 (`opencode.ai/install --version 1.18.14`),
  codex-cli 0.146.0 (openai/codex release tarball), codegraph 1.2.0 (npm),
  opencodex + ocx-relay (fresh clones at pinned commits), and the DNDSR
  externals (header-only release tarball + cfd_externals built from source).
  All executables install into system prefixes (`/usr/local/bin`, `/opt`);
  opencode's runtime stores live at `/opt/opencode-cache` and
  `/opt/opencode-config` (`XDG_CACHE_HOME`/`XDG_CONFIG_HOME` baked in), and
  the image home stays empty — the start-time uid/gid remap never chowns a
  large tree (no `usermod`, no recursive home chown, ~1s container start).
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
- `entrypoint.sh` — (1) remaps the generic image user (`cfd_agent`) to the
  invoking host user's uid/gid (`HOST_UID`/`HOST_GID`, enroot-style); (2) by
  default does NOT start opencodex — containers rely on the host-hosted ocx
  service via `--network host` (the workspace still snapshots the ocx config
  for record); (3) execs the requested command. Set `OPENCODEX_AUTOSTART=1`
  to start a container-local proxy (probe/start port from `$OCX_PORT`, else
  `config.json` `.port`, else 10100).
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
   `/home/cfd_agent/.codex` (`CODEX_HOME=/home/cfd_agent/.codex`): config.toml, the
   ocx profiles, catalog, AGENTS.md. Sessions/logs/DBs persist in the
   workspace copy.
2. **opencode** — `configs/opencode/` → `$WS/.sessions/opencode-config/` →
   `/home/cfd_agent/.config/opencode`: opencode.jsonc (apiKeys as
   `{env:OPENCODE_API_KEY_<PROVIDER>}` references), agents, commands, skills,
   MCP, themes. The data dir (`XDG_DATA_HOME=$WS/.sessions/opencode-data`)
   holds sessions/DB and the auth store (fresh or bind-mounted).
3. **opencodex** — `configs/opencodex/` → `$WS/.sessions/opencodex/` →
   `/home/cfd_agent/.opencodex`: config.json (apiKeys as
   `$OPENCODEX_<PROVIDER>_API_KEY` references). Runtime state (usage,
   artifacts, sqlite) persists in the workspace copy.
4. **bash** — `configs/bash/` → `$WS/.sessions/bash/` → `~/.bashrc`,
   `~/.bash_profile`, `~/.profile`, `~/.bash_logout` (the manager host's
   bashrc settings with host-specific `source ~/...` lines guarded so
   missing files stay silent), a friendly `~/.inputrc` (host bindings +
   case-insensitive completion, colored/visible completion stats, no bell),
   the host's `~/.alias`/`~/.envset`, and a persistent `~/.bash_history`.
   Override the whole set with `CONFIG_STACK/bash`. The image shell is
   Ubuntu's default bash (`/bin/bash`), and the shell starts in the
   contestant workspace (`WORKSPACE` env + docker `-w`).

The live host config stack is **never** used as the config source (no
fallback). Credentials are supplied separately: export the referenced env
vars on this host (forwarded into the container), or pass
`--host-credentials` to export the real keys from the live host configs
read-only. `--mount-host-configs` is the explicit, non-reproducible escape
hatch that mounts the live host dirs instead.

How each harness picks its config:

- **opencode** — the vendored `opencode.jsonc` at
  `/home/cfd_agent/.config/opencode` (+ omo-slim plugin, commands, skills from
  the same dir); auth from the data dir (`$XDG_DATA_HOME/opencode/`).
- **codex** — `$CODEX_HOME/config.toml` (= `/home/cfd_agent/.codex`, the
  vendored stack copy), plus profiles next to it. The user-level **ocx
  profile** (`ocx.config.toml` — deepseek-v4-flash routed through the
  opencodex proxy at 127.0.0.1:10109) is part of the stack; launch
  `codex -p ocx`, or `start.sh --harness codex --codex-profile ocx`.
- **opencodex** — the vendored `~/.opencodex/config.json`, snapshotted into
  the workspace for record/metadata. The container does NOT start a proxy by
  default: it relies on the host-hosted ocx service (with `--network host`,
  `127.0.0.1:10109` on this host is the host's daemon). Set
  `OPENCODEX_AUTOSTART=1` to opt into a container-local proxy (started on
  `$OCX_PORT`, else the config's port, else 10100, only when the port is
  free).

Note: codex **project-local** `.codex/config.toml` files cannot set provider
routing — codex ignores `model_provider`, `model_providers` and
`openai_base_url` there by design. The ocx routing therefore lives in the
user-level profile, not in contestant repos.

## Build

```bash
docker/build.sh                    # docker build (fresh official installs, pinned versions)
IMAGE=cfd-bench:test docker/build.sh
JOBS=8 docker/build.sh             # more parallel build jobs (default 4)
OPENCODE_VERSION=1.19.0 docker/build.sh   # switch a pinned version (see Dockerfile ARGs)
BUILD_NO_CACHE=1 docker/build.sh   # full rebuild, ignore the layer cache
BUILD_PROGRESS=plain docker/build.sh  # verbose per-step output (shows CACHED)
```

All pinned versions/commits are `ARG`s at the top of the Dockerfile
(opencode, codex, opencodex, ocx-relay, cfd_externals, the header-only
bundle); `build.sh` forwards them from the environment when set, so e.g.
`CFD_EXTERNALS_COMMIT=<sha> docker/build.sh` switches just that pin. doxygen
is installed from apt right before the externals build.

### Build caching

Docker caches per layer automatically (BuildKit): each `RUN`/`COPY`
instruction is a cache unit keyed on the instruction text + its inputs, and
unchanged layers are reused ("CACHED"). The cache lives in the Docker daemon
data root — `/var/lib/docker/buildkit/` here (`docker system df` reports it
under "Build Cache"; `docker buildx du` for a per-image breakdown). Image
layers themselves are content-addressed under `/var/lib/docker/overlay2/`.

What keeps cache hits high in this image:

- pinned URLs/commits/tags everywhere — the `curl`/`git fetch`/installer
  layers are deterministic, so their instructions never change;
- stable layer order — the heavy externals build sits below the harness
  installs, so harness tweaks never invalidate it;
- a small build context — `.dockerignore` excludes `.git`, `docker/.context`,
  `docker/configs`, etc.

What invalidates: editing a `RUN`/`COPY` line invalidates that layer and
everything after it (e.g. changing the apt package list re-runs the apt
layer and all later layers once; the base image and `ARG`/`ENV` layers stay
cached). The heavy layers use BuildKit **cache mounts**
(`/var/cache/apt`, `/root/.cache/pip`, `/root/.npm`, `/root/.bun-cache`), so
even when a layer must rebuild, the packages it downloads are reused from
`/var/lib/docker/buildkit/cache/` instead of re-fetched.

Housekeeping: `docker builder prune` removes dangling cache entries;
`docker system df` shows what is reclaimable. To share the cache across
machines/CI, push the image and build with `--cache-from <registry>/<image>`.

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

- the contestant workspace, mounted **rw at the container-internal path
  `/workspace`** — the host parent and every other host path are NOT
  mounted, so nothing outside the workspace is visible or writable,
- the vendored config stack (default `<repo>/docker/configs`, override with
  `CONFIG_STACK=/path/to/stack`), copied into `$WS/.sessions/` and mounted
  at the inside-docker-home paths (the bundle is visible at
  `/workspace/.sessions/` inside the container),
- `/workspace/.sessions/codex` → `/home/cfd_agent/.codex`,
- `/workspace/.sessions/opencode-config` → `/home/cfd_agent/.config/opencode`,
- `/workspace/.sessions/opencodex` → `/home/cfd_agent/.opencodex`,
- `/workspace/.sessions/bash` → `~/.bashrc`/`~/.profile`/`~/.bash_logout` +
  persistent `~/.bash_history`,
- `/workspace/.sessions/opencode-data` → the opencode data dir
  (`XDG_DATA_HOME=/workspace/.sessions/opencode-data`; fresh DB; auth via
  env or `--host-credentials`),
- the host codegraph index cache at `/home/cfd_agent/.codegraph` (container
  user's own cache path; no host path exposed).

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
  and bind-mounts (read-only) the harness credential stores: codex's openai
  credentials (`~/.codex/auth.json` — the ChatGPT-backend tokens; `codex
  login status` inside the container shows the account), opencode's auth
  store (`~/.local/share/opencode/{auth,account}.json` — every auth'd
  provider, including non-`sk-` keys like zai), while opencodex stays
  env-only (`OPENCODEX_*_API_KEY`; its config.json is already in the stack
  with `$VAR` references). Nothing is written to the workspace; the keys
  exist only in the container env / ro mounts.

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
- opencodex is an LLM router/proxy, not a harness: by default the container
  does not start one and relies on the host-hosted ocx (e.g. the user-level
  `ocx start --port 10109`). Set `OPENCODEX_AUTOSTART=1` to opt into a
  container-local proxy; when enabled, autostart is skipped if the port is
  already occupied. Port resolution: `OCX_PORT`, else `config.json` `.port`,
  else 10100.

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
  (read-only) the credential stores of all three harnesses: codex's openai
  credentials (`~/.codex/auth.json` — ChatGPT-backend tokens), opencode's
  auth store (`~/.local/share/opencode/{auth,account}.json` — all auth'd
  providers, not just the config's `sk-` apiKeys), and opencodex via
  `OPENCODEX_*_API_KEY` env (its config already lives in the stack). The
  keys exist only in the container env / ro mounts — never in the workspace
  record.
- Codex credentials have no env-placeholder mechanism: auth stays in
  `auth.json` (bind-mounted with `--host-credentials`, or `codex login`
  inside the container) or in a custom provider's `env_key`.

### Persistent sessions, bundled with the workspace

`start.sh` (default mode) points each harness at `$WS/.sessions/` inside the
contestant workspace itself:

- `codex` → `$WS/.sessions/codex` mounted at `/home/cfd_agent/.codex` with
  `CODEX_HOME=/home/cfd_agent/.codex` (vendored config copy + sessions, logs,
  sqlite DBs all persist in the workspace; with `--host-credentials`,
  `auth.json` is bind-mounted from `~/.codex`, never stored).
- `opencode` → `XDG_DATA_HOME=/workspace/.sessions/opencode-data` (a fresh
  `opencode.db`, logs, storage), with `auth.json`/`account.json` optionally
  bind-mounted from `~/.local/share/opencode` (`--host-credentials`; never
  copied).
- `opencodex` → `$WS/.sessions/opencodex` mounted at `/home/cfd_agent/.opencodex`
  (vendored config copy + runtime state).

So a contestant run leaves its full session history on disk in the working
directory. `.sessions/` is added to the workspace's `.git/info/exclude`
(`setup-workspace.sh` at creation, `start.sh` as a safety net for existing
workspaces).

### Container lifecycle

The container is always launched with `--rm`, and `start.sh` additionally
traps EXIT/INT/TERM and force-removes the named container, so no `bench-*`
instance survives the launcher even when the client is killed.

### User mapping (enroot-style)

The image is **user-agnostic**: it ships one generic user (`cfd_agent`, uid/
gid 1000) and bakes in nothing about the build machine's user. At container
start, `start.sh` passes `HOST_UID`/`HOST_GID`; the entrypoint (entered as
root) remaps `cfd_agent` to those ids (`groupmod`/`usermod`), chowns the
container home, then drops privileges with `setpriv` before exec'ing the
command. The process therefore owns the same uid/gid as the invoking host
user, so the mounted host dirs (workspace, configs, `~/.codegraph`) and any
files created in the container belong to the right user — no rebuild needed
on any machine. A bare `docker run` without `HOST_UID`/`HOST_GID` runs as
root inside the container.

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
  --security-opt apparmor=unconfined --user root:root \
  -e HOME=/home/cfd_agent \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$CFG/codex:/home/cfd_agent/.codex" \
  -v "$CFG/opencode:/home/cfd_agent/.config/opencode" \
  -v "$CFG/opencodex:/home/cfd_agent/.opencodex" \
  -v "$HOME/.codex/auth.json:/home/cfd_agent/.codex/auth.json" \
  -e OPENCODE_API_KEY_DEEPSEEK="${OPENCODE_API_KEY_DEEPSEEK:?export the referenced env vars}" \
  -e OPENCODEX_DEEPSEEK_API_KEY="${OPENCODEX_DEEPSEEK_API_KEY:?}" \
  -v "$BENCH_ROOT:$BENCH_ROOT" -w "$WS" \
  cfd-bench:latest codex exec --json -p ocx --skip-git-repo-check \
    "Reply with exactly: PONG"

docker run --rm --network host --security-opt seccomp=unconfined \
  --security-opt apparmor=unconfined --user root:root \
  -e HOME=/home/cfd_agent \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$CFG/codex:/home/cfd_agent/.codex" \
  -v "$CFG/opencode:/home/cfd_agent/.config/opencode" \
  -v "$CFG/opencodex:/home/cfd_agent/.opencodex" \
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
docker/scripts/setup-workspace.sh codex/gpt56/08 codex/gpt56/init
docker/scripts/start.sh --workspace workspace/codex/gpt56/08
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

---
name: harness-stack-management
description: Manage the benchmark harness stack in this repository — the Docker runtime image, vendored opencode/codex/opencodex config stack, contestant workspace setup, and container launch/session persistence. Use when asked to build or rebuild the cfd-bench image, start/stop/attach contestant containers (docker/scripts/start.sh, --harness, --host-credentials, OCX_PORT, --detach), create fresh contestant workspaces (setup-workspace.sh), sync or redact the vendored configs (sync-configs.sh), pin/install opencode plugins, or troubleshoot container/session/config issues (user remap, ocx reachability, force-removal, missing sessions).
---

# Harness Stack Management

Operate and maintain the benchmark container harness: image build, vendored
config stack, workspace setup, and container launch. All entry points are
shell scripts under `docker/`; detailed references live in `references/`.

## Component map

| Component | Location | Purpose |
|---|---|---|
| Image build | `docker/Dockerfile`, `docker/build.sh` | Self-contained runtime; official pinned installs only |
| Plugin manifest | `docker/opencode-plugins.json` | opencode plugins pinned at build time |
| Config stack | `docker/configs/` (+ `docker/scripts/sync-configs.sh`) | Credential-free configs installed at container start |
| Workspace setup | `docker/scripts/setup-workspace.sh` | Fresh contestant workspace from a template branch |
| Launch | `docker/scripts/start.sh`, `docker/entrypoint.sh` | User remap + config stack + session persistence |

## Core workflows

### Build or rebuild the image
Run `docker/build.sh` from the repo root. It forwards proxy envs only when
they are already exported (loopback proxies are auto-detected) and forwards
version/commit overrides from the environment. Read
[references/image-build.md](references/image-build.md) before changing pinned
versions, plugin specs, or layer order.

### Sync the vendored config stack
Run `docker/scripts/sync-configs.sh --env-mode` to regenerate `docker/configs/`
from the live host configs, redact secrets, and rewrite apiKeys to env
references. Never commit unredacted credentials — the script exits non-zero
if any remain. See [references/config-stack.md](references/config-stack.md).

### Create a contestant workspace
Run `docker/scripts/setup-workspace.sh <path> <harness>/<model>/init` to clone the template,
pin the benchmark submodule, remove origin, initialize codegraph, symlink
external, and capture the required pre-run environment snapshot. The snapshot
binds the exact initial commit to a normalized init-branch name detected from
the matching local or remote-tracking ref; setup fails closed if this
provenance cannot be captured. Relative paths land under `workspace/`;
absolute paths are used as-is.

### Launch a container
Run `bash docker/scripts/start.sh --workspace DIR --harness codex [--detach]`
(or select `opencode` / `claude`). Claude Code state is seeded and persisted at
`<workspace>/.sessions/claude`, mounted as `/home/cfd_agent/.claude`; preserve
the whole directory for later manager-side post-processing.
It installs the vendored stack into `DIR/.sessions/` and mounts them into the
container at `/workspace`. Use `--detach` for runs that outlive a terminal;
interactive mode passes signals through to the container (no force-remove
unless `--force-remove`). See
[references/launch-and-workspace.md](references/launch-and-workspace.md) for
the full flag/env reference and troubleshooting.

## Guardrails

- Never bake host configs or binaries into the image: build-time installs
  come from official sources at pinned versions; configs are injected at
  container start.
- Never commit credentials into `docker/configs/`; always regenerate with
  `sync-configs.sh` and check its verification output.
- Long agent runs must use `--detach`; interactive mode does not intercept
  signals by default — pass `--force-remove` to restore the launcher
  force-remove trap (see
  `notes/2026-08-08-docker-force-removal-incident.md`).
- opencodex is a router, not a harness: the container defaults to a
  host-hosted ocx service (`OPENCODEX_AUTOSTART=0`); do not enable the
  in-container proxy unless the host service is unavailable.
- This is the manager repo: pushing, switching branches, and other remote
  mutations require explicit user authorization.

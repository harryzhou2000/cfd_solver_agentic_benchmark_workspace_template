# Image build reference

## `docker/build.sh`

- Builds `cfd-bench:latest` (override `IMAGE`); `JOBS` controls the externals
  build.
- Forwards proxy build-args + `--network host` automatically when
  `HTTP(S)_PROXY` is already exported and on loopback; `BUILD_PROXY=1`
  forces it. The build does not source any proxy script.
- `BUILD_NO_CACHE=1`, `BUILD_PROGRESS=<plain|tty>`.
- Forwards version/commit overrides from env: `OPENCODE_VERSION`,
  `CODEX_VERSION`, `OPENCODEX_REPO`/`OPENCODEX_COMMIT`,
  `OCX_RELAY_REPO`/`OCX_RELAY_COMMIT`,
  `EXTERNAL_HEADERONLYS_REPO`/`EXTERNAL_HEADERONLYS_TAG`,
  `CFD_EXTERNALS_REPO`/`CFD_EXTERNALS_COMMIT`.

## Pinned versions (Dockerfile ARGs, current)

| Component | Pin |
|---|---|
| Base | ubuntu:24.04 |
| opencode | 1.18.18 |
| codex-cli | 0.148.0 |
| Node.js | 24.18.0 |
| bun | 1.3.14 |
| uv | 0.12.1 |
| codegraph | 1.2.0 |
| opencodex | commit ea7f5a697... |
| ocx-relay | commit 886298b38... |
| external_headeronlys | tag v0.1.0 |
| cfd_externals | commit 942cf6700... |
| TeX Live | 2023 (Ubuntu 24.04 apt: texlive-full + biber + latexmk) |

Everything is installed fresh from official sources; nothing is staged or
copied from the host. Version bumps go through the Dockerfile ARGs, not
edits inside RUN steps.

## Layer/caching notes

- apt, pip, npm, and bun caches use BuildKit cache mounts, so rebuilds
  re-download only deltas.
- Order matters: build stack + uv + openmpi (apt) → doxygen → externals
  (heavy source build) → harnesses. Keep heavy layers below frequently
  changing ones.
- texlive-full is installed after the harness binaries (current layout),
  so this rebuild reused the cached harness layers; future harness version
  bumps will also rebuild the texlive layer.
- The image is user-agnostic: one generic `cfd_agent` (uid/gid 1000), home
  kept empty, opencode stores under `/opt` (XDG_*), entrypoint remaps at
  start.

## Plugins

- Manifest `docker/opencode-plugins.json`; installed by
  `docker/scripts/install-opencode-plugins.sh` with the official
  `opencode plugin -g <spec>` installer (never from the host).
- Current pins: `opencode-goal-plugin@0.8.1` (npm),
  `oh-my-opencode-slim@2.2.10`.
- The installer verifies each spec is recorded in the global config and
  fails the build if any is missing (prevents partial installs in cached
  layers).

## Troubleshooting

- Build fails at `groupadd`: a stale uid/gid 1000 owner in the base image —
  the Dockerfile deletes the stock `ubuntu` user before creating
  `cfd_agent`.
- `curl`/`git` timeouts: export `HTTP_PROXY`/`HTTPS_PROXY` (e.g. source
  `~/.setproxy.sh`) before `docker/build.sh`; loopback proxies are
  auto-detected.
- Plugin missing after build: rerun the affected step with `--no-cache`
  (flaky first-run npm installs are the known cause).

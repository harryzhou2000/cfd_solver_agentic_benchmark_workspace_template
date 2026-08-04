# Benchmark contestant runtime (Docker)

A reproducible, interactive runtime for benchmark contestants. The image
records the current host environment so every contestant runs inside the same
steady container: opencode (with omo-slim + goal plugin + rate-limit-retry),
codex, the opencodex provider proxy and ocx-relay, codegraph, the CFD build
toolchain (GCC 13, CMake, Ninja, OpenMPI 5.0.9), and the shared DNDSR
externals.

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
- `scripts/` — `start.sh` (interactive launcher with workspace + persistent
  data mounts) and `setup-workspace.sh` (fresh contestant workspace:
  `git remote rm origin && codegraph init && ln -s ../opencode_omoslim_deepseek/external .`).

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
- `~/.codex` (sessions, DBs, `auth.json` — codex login is manual per instance),
- `~/.config/opencode` + `~/.local/share/opencode` (opencode config, plugins,
  session DB, auth),
- `~/.opencodex` (proxy config incl. provider keys + state),
- `~/.codegraph` (index cache).

`--image-config` skips mounting the live config dirs and uses the baked
redacted snapshots instead.

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

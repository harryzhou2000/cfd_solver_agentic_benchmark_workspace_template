# Launch and workspace reference

## start.sh

Run with bash: `bash docker/scripts/start.sh ...` (the script refuses other
shells).

### Flags

| Flag | Meaning |
|---|---|
| `--workspace DIR` (or positional) | Host workspace dir; mounted at `/workspace` |
| `--harness shell\|codex\|opencode\|claude` | Default command in the container (default `shell`) |
| `--codex-profile ocx` | Run `codex -p ocx` (profile-based provider routing) |
| `--name NAME` / `-n` | Container name (default `bench-<workspace-basename>`) |
| `--cpus N` | CPU quota (default 4) |
| `--detach` | Detached run; survives terminal close; stop with `docker stop <name>` |
| `--force-remove` | Interactive-only: launcher trap force-removes the container on EXIT/INT/TERM/HUP (default: signals pass through) |
| `--host-credentials` | Read real keys from this host's live configs into container env + ro auth binds |
| `--image-config` | No config stack/session bundle; bare image state |
| `--mount-host-configs` | Mount live host config dirs over the stack (escape hatch; non-reproducible) |
| `-- cmd...` | Override the container command |

### Env vars

| Var | Meaning |
|---|---|
| `IMAGE` | Image tag (default `cfd-bench:latest`) |
| `CONFIG_STACK` | Config stack dir (default `<repo>/docker/configs`) |
| `CPUS` | Same as `--cpus` |
| `OCX_PORT` | ocx port passed into the container (probe port; default 10100, host service commonly 10109) |
| `OPENCODEX_AUTOSTART` | `1` starts a container-local ocx (default `0`; host-hosted service assumed) |
| `HTTP(S)_PROXY`, `ALL_PROXY`, `NO_PROXY` | Forwarded when already exported; loopback hosts kept out of `NO_PROXY` |
| `OPENCODE_*`, `OPENCODEX_*`, `ANTHROPIC_API_KEY`, `ANTHROPIC_AUTH_TOKEN` | Credential envs already exported on the host are forwarded into the container |

### Behavior

- The workspace is the only host path mounted (`/workspace`); the host parent
  is not visible inside the container.
- A per-workspace lock file (`$WS/.sessions/docker.lock`) records the
  container instance that owns the workspace (`container=`,
  `launcher_pid=`, `started=`, `workspace=`) and is bind-mounted
  read-only into the container, so the docker executor can neither modify nor
  unlink it. The launcher refuses to start when the lock exists, and refuses
  to start when a container with the chosen name already exists (no silent
  force-remove). The lock is removed when the container exits normally:
  interactive modes remove it via an EXIT trap, `--detach` removes it from a
  `docker wait` watcher when the container stops. A stale lock (SIGKILLed
  launcher) blocks relaunch by design; remove it manually only after
  confirming no container is running for that workspace.
- Network is host (`--network host`), so `127.0.0.1:<OCX_PORT>` reaches a
  host-side ocx service.
- seccomp/apparmor are relaxed (`--security-opt ...=unconfined`) so codex's
  bundled bwrap sandbox works.
- Interactive mode runs foreground `docker run -it --rm` and does not
  intercept signals: Ctrl-C / SIGTERM / SIGHUP pass through to the container
  (docker --sig-proxy), which decides how to exit; `--rm` removes it once it
  exits. `--force-remove` opts back into the launcher trap (EXIT/INT/TERM/HUP
  + `docker rm -f`). `--detach` runs `docker run -dit --rm` with no trap —
  only `docker stop` ends it.

### Entrypoint (`docker/entrypoint.sh`)

- Remaps the generic image user `cfd_agent` to `HOST_UID`/`HOST_GID` by
  rewriting `/etc/passwd`/`/etc/group` (no `usermod` — avoids overlayfs home
  copy-up), then `setpriv` drops privileges.
- `WORKSPACE` env selects the start directory.
- Seeds `/etc/skel` dotfiles (default Ubuntu bash setup) into an empty home.
- ocx probe port resolution: `$OCX_PORT` override → `.port` in
  `~/.opencodex/config.json` → `10100`. Starts an in-container proxy only
  when `OPENCODEX_AUTOSTART=1` AND nothing is listening; otherwise it warns
  if no host service is reachable.

## Session persistence (`$WS/.sessions/`)

| Dir | In container | Contents |
|---|---|---|
| `codex/` | `~/.codex` (CODEX_HOME) | codex configs, sessions, DB |
| `opencode-config/` | `~/.config/opencode` | opencode config |
| `opencode-data/opencode/` | `$XDG_DATA_HOME/opencode` | opencode sessions, DB, auth |
| `opencodex/` | `~/.opencodex` | ocx config + runtime state |
| `claude/` | `~/.claude`, `~/.claude.json` | Claude Code config (settings, CLAUDE.md, sanitized user config) + sessions/history/daemon state |
| `bash/` | `~/.bashrc` etc. | bash setup + history |

`.sessions/`, `.opencode/`, `.eval/` are git-excluded per workspace
(setup-workspace.sh writes `.git/info/exclude`; start.sh re-checks).

## setup-workspace.sh

`docker/scripts/setup-workspace.sh <path> <harness>/<model>/init`

- Relative paths resolve under `<repo>/workspace/` (override `WS_ROOT`);
  absolute paths are used as-is. Refuses to overwrite an existing dir.
- Steps: clone the required init branch, `git submodule update
  --init --recursive`, create git-excluded session dirs, `git remote rm
  origin`, `codegraph init` (when available), symlink `external` ->
  `EXTERNAL_SRC` (default `/opt/external`, the image-built externals), then
  write the required pre-run snapshot via `evaluation/tools/env_snapshot.py`.
  Snapshot capture binds the exact commit to the matching local or
  remote-tracking init ref; setup aborts if provenance is missing or ambiguous.

## Troubleshooting

- `[entrypoint] mapping cfd_agent -> uid=...` stall: the passwd rewrite +
  shallow chown is intentional (no recursive chown); check mounted host dirs
  are owned by the invoking user.
- `ERROR: workspace is locked ...`: a launcher for this workspace is active
  or left a stale lock after being killed. Confirm `docker ps -a` shows no
  container for this workspace, then remove
  `$WS/.sessions/docker.lock` manually.
- `ERROR: container '<name>' already exists ...`: remove or rename the
  existing container instead of relaunching (the launcher no longer
  force-removes it).
- Container still running after the terminal closed: interactive mode no
  longer force-removes on signal, so the container survives; reattach with
  `docker attach <name>` or stop it with `docker stop <name>` (rerun with
  `--detach` or `--force-remove` to avoid this next time).
- `WARNING: nothing listening on 127.0.0.1:<port>`: start the host-side ocx
  (`ocx start --port 10109`) or set `OCX_PORT`/`OPENCODEX_AUTOSTART=1`.
- See `notes/2026-08-08-docker-force-removal-incident.md` for the signal
  forensics of the 2026-08-08 mass container kill.

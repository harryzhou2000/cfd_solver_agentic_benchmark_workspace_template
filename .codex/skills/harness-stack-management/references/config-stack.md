# Config stack reference

## Layout (`docker/configs/`)

- `opencode/` — `opencode.jsonc` (mirror of the host global config; aux
  files from `~/tools/opencode_config/opencode/`: oh-my-opencode-slim,
  package.json, agents/, commands/, mcp/, rules/, skills/, themes/)
- `codex/` — `config.toml`, `opencodex.config.toml`, `ocx.config.toml`,
  `opencodex-catalog.json`, `AGENTS.md`
- `opencodex/` — `config.json`
- `claude/` — Claude Code user config: `settings.json`, `CLAUDE.md`,
  `statusline-command.sh`, `agents/`
- `bash/` — `.bashrc`/`.bash_profile`/`.profile`/`.inputrc`/`.alias`/`.envset`

All files are credential-free: apiKeys are either `REDACTED`, opencode
`{env:OPENCODE_API_KEY_<PROVIDER>}` placeholders, or opencodex
`$OPENCODEX_<PROVIDER>_API_KEY` env references.

## Regenerating (`docker/scripts/sync-configs.sh`)

1. Copies the current host configs (opencode, codex, opencodex, claude, bash
   dotfiles). Claude credential state (`~/.claude/.credentials.json`,
   `remote-settings.json`, daemon/session data) is never copied.
2. Redacts `apiKey`/`key`/`sk-...` values.
3. Guards host-absolute `source ~/...` lines in the bash files.
4. With `--env-mode`: rewrites opencode apiKeys to per-provider
   `{env:...}` placeholders and opencodex apiKeys to `$OPENCODEX_*`.
5. Rewrites `${HOME}/.codex` to `/home/cfd_agent/.codex` in the codex tomls
   (container CODEX_HOME).
6. Verifies no secrets remain; exits non-zero on any hit.

Use `--env-mode` whenever the goal is a usable, committable stack.

## Goal plugin

The goal plugin is pinned to `npm:opencode-goal-plugin@0.8.1` in
`docker/configs/opencode/opencode.jsonc` (mirrors the global
`~/.config/opencode/opencode.jsonc`) and in `docker/opencode-plugins.json`
(image build manifest). Keep all three in sync when bumping.

## Supplying credentials at runtime

- Exported `OPENCODE_*` / `OPENCODEX_*` env vars on the host are forwarded
  into the container by start.sh (no host file reads); exported
  `ANTHROPIC_API_KEY` / `ANTHROPIC_AUTH_TOKEN` are forwarded too.
- `--host-credentials` additionally reads the live host configs (opencode
  providers → `OPENCODE_API_KEY_<PROVIDER>`, opencodex providers →
  `OPENCODEX_<PROVIDER>_API_KEY`) into container env and ro-mounts codex
  `auth.json`, opencode `auth.json`/`account.json`, and Claude Code's
  `~/.claude/.credentials.json` (OAuth). Nothing is written into the
  workspace.
- Codex auth is file-based (`auth.json`); there is no codex env-key path.

## Rules

- Never commit unredacted keys; if `sync-configs.sh` verification fails, fix
  the source or redaction before committing.
- The vendored stack is the committed record of the host stack; the
  container never falls back to live host configs unless
  `--mount-host-configs` is used.

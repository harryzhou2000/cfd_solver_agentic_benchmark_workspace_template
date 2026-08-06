# Configuration Snapshot Specification

## Purpose

Each run may have a slightly different config stack (per-branch `AGENTS.md`,
user-level codex/opencode/opencodex configs, plugin versions, shell init,
proxy settings). The snapshot must contain those configs **as much as
possible** so a run is reproducible and auditable.

Produced by `evaluation/tools/extract_configs.py` (cfdeval package:
`cfdeval.configs`), embedded as `configs.json` (schema:
`evaluation/schemas/configs.schema.json`).

## Captured config classes

| Role | Files |
|---|---|
| `codex` | `~/.codex/config.toml`, `ocx.config.toml`, `opencodex.config.toml`, `AGENTS.md`, `version.json`, `codex-runtime.json`, `opencodex-catalog.json`, `models_cache.json`, `rules/*.md` |
| `plugin_manifest` | `~/.codex/plugins/cache/*/<name>/<version>/.codex-plugin/plugin.json` and `.app.json` |
| `opencode` | `~/.config/opencode/opencode.jsonc`, `tui.json`, `agents.json`, `agent/**`, `command/**`, `rules/**` |
| `opencodex` | `~/.opencodex/config.json`, `version.json`, `codex-runtime.json`, `responses-state.json`, `codex-quota-cache.json`, `catalog-backup*.json` |
| `shell` | `~/.bashrc`, `~/.zshrc`, `~/.profile`, `~/.setproxy.sh` |
| `workspace` | `<workspace>/AGENTS.md`, `.codex/config.toml`, `.opencode/opencode.jsonc`, `.opencodex/config.json`, `.gitmodules`, `external` symlink target |
| `benchmark` | submodule `TASK.md`, `examiner/SCORING_RUBRIC.md`, `README_EXAMINER.md` |

## Redaction policy

- **Credential files** (`auth.json`, `codex-accounts.json`, `admin-api-token`,
  `installation_id`) are recorded as presence + sha256 only; content is never
  embedded.
- Secret-like values in every other file (sk- keys, Bearer tokens,
  api keys/tokens/secrets/passwords, proxy `user:pass@` URLs, long base64)
  are replaced with `***REDACTED***`; `redaction_hits` counts replacements.
- Content is capped per file (default 128 KiB; catalogs 512 KiB);
  `truncated` marks capped files.

The redactor lives in `cfdeval.redact` and is unit-testable; the policy is
conservative (over-redaction is acceptable, leakage is not).
